#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lcd_face_ui.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define WIFI_NAMESPACE              "wifi_web"
#define WIFI_KEY_SSID               "ssid"
#define WIFI_KEY_PASSWORD           "password"

#define WIFI_SSID_MAX_LEN           32
#define WIFI_PASSWORD_MAX_LEN       63
#define HTTP_BODY_MAX_LEN           384
#define WIFI_SCAN_MAX_AP            20
#define WIFI_AP_SUFFIX_CHARS        13

typedef enum {
    APP_NET_STATE_CONFIG = 0,
    APP_NET_STATE_CONNECTING,
    APP_NET_STATE_CONNECTED,
    APP_NET_STATE_FAILED,
} app_net_state_t;

typedef struct {
    lcd_face_scene_t scene;
    char line1[24];
    char line2[48];
} lcd_status_msg_t;

typedef struct {
    app_net_state_t state;
    bool configured;
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char sta_ip[16];
    char ap_ssid[WIFI_SSID_MAX_LEN + 1];
    char ap_ip[16];
    char last_error[64];
    int retry;
} app_status_snapshot_t;

extern const unsigned char index_html_start[] asm("_binary_index_html_start");
extern const unsigned char index_html_end[] asm("_binary_index_html_end");

static const char *TAG = "wifi_web_config";

static SemaphoreHandle_t s_status_lock;
static QueueHandle_t s_lcd_queue;
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_httpd;

static app_net_state_t s_net_state = APP_NET_STATE_CONFIG;
static bool s_has_credentials;
static bool s_ignore_next_disconnect;
static char s_current_ssid[WIFI_SSID_MAX_LEN + 1];
static char s_sta_ip[16] = "0.0.0.0";
static char s_ap_ssid[WIFI_SSID_MAX_LEN + 1] = CONFIG_SPARKBOT_WEB_CONFIG_AP_SSID;
static char s_ap_ip[16] = "192.168.4.1";
static char s_last_error[64] = "No WiFi saved";
static int s_retry_count;

static void copy_string(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0) {
        return;
    }
    snprintf(dst, dst_len, "%s", src ? src : "");
}

static const char *state_to_string(app_net_state_t state)
{
    switch (state) {
    case APP_NET_STATE_CONFIG:
        return "config";
    case APP_NET_STATE_CONNECTING:
        return "connecting";
    case APP_NET_STATE_CONNECTED:
        return "connected";
    case APP_NET_STATE_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

static const char *auth_mode_to_string(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:
        return "WAPI";
    default:
        return "UNKNOWN";
    }
}

static void build_ap_ssid(char *ssid, size_t ssid_len)
{
    if (ssid_len == 0) {
        return;
    }

    const char *base = CONFIG_SPARKBOT_WEB_CONFIG_AP_SSID;
    if (!base || base[0] == '\0') {
        base = "SparkBot";
    }

    uint8_t mac[6] = {0};
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_AP, mac);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Read SoftAP MAC failed: %s, use base AP SSID", esp_err_to_name(err));
        copy_string(ssid, ssid_len, base);
        return;
    }

    const size_t max_ssid_chars = ssid_len - 1;
    const size_t max_base_len = max_ssid_chars > WIFI_AP_SUFFIX_CHARS ?
                                max_ssid_chars - WIFI_AP_SUFFIX_CHARS : 0;
    size_t base_len = strlen(base);
    if (base_len > max_base_len) {
        base_len = max_base_len;
    }

    snprintf(ssid, ssid_len, "%.*s-%02X%02X%02X%02X%02X%02X",
             (int)base_len, base, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void lcd_post(lcd_face_scene_t scene, const char *line1, const char *line2)
{
    if (!s_lcd_queue) {
        return;
    }

    lcd_status_msg_t msg = {
        .scene = scene,
    };
    copy_string(msg.line1, sizeof(msg.line1), line1);
    copy_string(msg.line2, sizeof(msg.line2), line2);

    if (xQueueSend(s_lcd_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "LCD status queue full, drop %s/%s", msg.line1, msg.line2);
    }
}

static void lcd_post_config_prompt(void)
{
    char ap_ssid[WIFI_SSID_MAX_LEN + 1] = {0};

    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    copy_string(ap_ssid, sizeof(ap_ssid), s_ap_ssid);
    xSemaphoreGive(s_status_lock);

    lcd_post(LCD_FACE_SCENE_IDLE, "JOIN AP", ap_ssid[0] ? ap_ssid : "CONFIG AP");
}

static void lcd_post_web_prompt(void)
{
    char ap_ip[16] = {0};
    char web_addr[24] = {0};

    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    copy_string(ap_ip, sizeof(ap_ip), s_ap_ip);
    xSemaphoreGive(s_status_lock);

    if (ap_ip[0] == '\0' || strcmp(ap_ip, "0.0.0.0") == 0) {
        copy_string(ap_ip, sizeof(ap_ip), "192.168.4.1");
    }

    if (CONFIG_SPARKBOT_WEB_CONFIG_HTTP_PORT == 80) {
        copy_string(web_addr, sizeof(web_addr), ap_ip);
    } else {
        snprintf(web_addr, sizeof(web_addr), "%s:%d", ap_ip, CONFIG_SPARKBOT_WEB_CONFIG_HTTP_PORT);
    }

    lcd_post(LCD_FACE_SCENE_SURPRISED, web_addr, "OPEN WEB");
}

static void lcd_status_task(void *arg)
{
    (void)arg;

    ESP_ERROR_CHECK(lcd_face_ui_init());
    lcd_face_ui_show_scene(LCD_FACE_SCENE_BOOT, "BOOT", "WEB WIFI");

    lcd_status_msg_t msg;
    while (true) {
        if (xQueueReceive(s_lcd_queue, &msg, portMAX_DELAY) == pdTRUE) {
            lcd_face_ui_show_scene(msg.scene, msg.line1, msg.line2);
        }
    }
}

static void status_get_snapshot(app_status_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));

    if (s_status_lock) {
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
    }

    snapshot->state = s_net_state;
    snapshot->configured = s_has_credentials;
    snapshot->retry = s_retry_count;
    copy_string(snapshot->ssid, sizeof(snapshot->ssid), s_current_ssid);
    copy_string(snapshot->sta_ip, sizeof(snapshot->sta_ip), s_sta_ip);
    copy_string(snapshot->ap_ssid, sizeof(snapshot->ap_ssid), s_ap_ssid);
    copy_string(snapshot->ap_ip, sizeof(snapshot->ap_ip), s_ap_ip);
    copy_string(snapshot->last_error, sizeof(snapshot->last_error), s_last_error);

    if (s_status_lock) {
        xSemaphoreGive(s_status_lock);
    }
}

static void update_ap_ip_string(void)
{
    if (!s_ap_netif) {
        return;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
        char ip_buf[16] = {0};
        esp_ip4addr_ntoa(&ip_info.ip, ip_buf, sizeof(ip_buf));

        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        copy_string(s_ap_ip, sizeof(s_ap_ip), ip_buf);
        xSemaphoreGive(s_status_lock);
    }
}

static void set_config_state(const char *message)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_net_state = APP_NET_STATE_CONFIG;
    s_retry_count = 0;
    copy_string(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
    copy_string(s_last_error, sizeof(s_last_error), message);
    xSemaphoreGive(s_status_lock);

    lcd_post_config_prompt();
}

static esp_err_t nvs_load_wifi(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = ssid_len;
    err = nvs_get_str(nvs, WIFI_KEY_SSID, ssid, &len);
    if (err != ESP_OK || ssid[0] == '\0') {
        nvs_close(nvs);
        return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    }

    len = password_len;
    err = nvs_get_str(nvs, WIFI_KEY_PASSWORD, password, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        password[0] = '\0';
        err = ESP_OK;
    }

    nvs_close(nvs);
    return err;
}

static esp_err_t nvs_save_wifi(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs), TAG, "open nvs failed");
    esp_err_t err = nvs_set_str(nvs, WIFI_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, WIFI_KEY_PASSWORD, password ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t nvs_forget_wifi(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs), TAG, "open nvs failed");
    esp_err_t err_ssid = nvs_erase_key(nvs, WIFI_KEY_SSID);
    esp_err_t err_pass = nvs_erase_key(nvs, WIFI_KEY_PASSWORD);
    esp_err_t err_commit = nvs_commit(nvs);
    nvs_close(nvs);

    if (err_ssid != ESP_OK && err_ssid != ESP_ERR_NVS_NOT_FOUND) {
        return err_ssid;
    }
    if (err_pass != ESP_OK && err_pass != ESP_ERR_NVS_NOT_FOUND) {
        return err_pass;
    }
    return err_commit;
}

static esp_err_t start_sta_connect(const char *ssid, const char *password, bool save_to_nvs)
{
    if (!ssid || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (save_to_nvs) {
        ESP_RETURN_ON_ERROR(nvs_save_wifi(ssid, password), TAG, "save wifi config failed");
    }

    wifi_config_t wifi_config = {0};
    copy_string((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), ssid);
    copy_string((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), password);
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = (password && password[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_ignore_next_disconnect = true;
    xSemaphoreGive(s_status_lock);

    esp_err_t disconnect_err = esp_wifi_disconnect();
    if (disconnect_err == ESP_ERR_WIFI_NOT_CONNECT) {
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        s_ignore_next_disconnect = false;
        xSemaphoreGive(s_status_lock);
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set sta config failed");

    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_net_state = APP_NET_STATE_CONNECTING;
    s_has_credentials = true;
    s_retry_count = 0;
    copy_string(s_current_ssid, sizeof(s_current_ssid), ssid);
    copy_string(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
    copy_string(s_last_error, sizeof(s_last_error), "Connecting");
    xSemaphoreGive(s_status_lock);

    lcd_post(LCD_FACE_SCENE_WINK, "JOIN WIFI", ssid);
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        s_net_state = APP_NET_STATE_FAILED;
        copy_string(s_last_error, sizeof(s_last_error), esp_err_to_name(err));
        xSemaphoreGive(s_status_lock);
        lcd_post(LCD_FACE_SCENE_ANGRY, "FAILED", "CONNECT");
    }

    return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        update_ap_ip_string();
        ESP_LOGI(TAG, "Config AP started: %s, http://%s", s_ap_ssid, s_ap_ip);
        lcd_post_config_prompt();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "A browser device joined the config AP");
        lcd_post_web_prompt();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        bool ignore_disconnect = false;
        bool has_credentials = false;
        bool can_retry = false;
        int retry = 0;

        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        has_credentials = s_has_credentials;
        if (s_ignore_next_disconnect) {
            s_ignore_next_disconnect = false;
            ignore_disconnect = true;
        } else if (!has_credentials) {
            s_net_state = APP_NET_STATE_CONFIG;
            s_retry_count = 0;
            copy_string(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
            copy_string(s_last_error, sizeof(s_last_error), "No WiFi saved");
        } else if (s_retry_count < CONFIG_SPARKBOT_WEB_CONFIG_MAX_STA_RETRY) {
            s_net_state = APP_NET_STATE_CONNECTING;
            s_retry_count++;
            retry = s_retry_count;
            snprintf(s_last_error, sizeof(s_last_error), "Retry %d reason %d", retry, event->reason);
            can_retry = true;
        } else {
            s_net_state = APP_NET_STATE_FAILED;
            snprintf(s_last_error, sizeof(s_last_error), "Failed reason %d", event->reason);
        }
        xSemaphoreGive(s_status_lock);

        if (ignore_disconnect) {
            ESP_LOGI(TAG, "Ignore planned STA disconnect");
            return;
        }

        if (can_retry) {
            ESP_LOGW(TAG, "WiFi disconnected, retry %d", retry);
            lcd_post(LCD_FACE_SCENE_WINK, "RETRY", "CONNECTING");
            esp_wifi_connect();
        } else if (!has_credentials) {
            lcd_post_config_prompt();
        } else {
            ESP_LOGW(TAG, "WiFi connect failed, reason=%d", event->reason);
            lcd_post(LCD_FACE_SCENE_ANGRY, "FAILED", "CHECK PASS");
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip_buf[16] = {0};
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_buf, sizeof(ip_buf));

        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        s_net_state = APP_NET_STATE_CONNECTED;
        s_retry_count = 0;
        copy_string(s_sta_ip, sizeof(s_sta_ip), ip_buf);
        copy_string(s_last_error, sizeof(s_last_error), "OK");
        xSemaphoreGive(s_status_lock);

        ESP_LOGI(TAG, "WiFi connected, STA IP: %s", ip_buf);
        lcd_post(LCD_FACE_SCENE_HAPPY, "ONLINE", ip_buf);
        return;
    }
}

static esp_err_t wifi_start_apsta(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_ap_netif, ESP_FAIL, TAG, "create ap netif failed");
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(sta_netif, ESP_FAIL, TAG, "create sta netif failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL),
                        TAG, "register wifi event failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL),
                        TAG, "register ip event failed");

    wifi_config_t ap_config = {0};
    char ap_ssid[sizeof(ap_config.ap.ssid)] = {0};
    build_ap_ssid(ap_ssid, sizeof(ap_ssid));
    copy_string((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), ap_ssid);
    copy_string(s_ap_ssid, sizeof(s_ap_ssid), ap_ssid);
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.channel = CONFIG_SPARKBOT_WEB_CONFIG_AP_CHANNEL;
    ap_config.ap.max_connection = 4;
    ap_config.ap.pmf_cfg.required = false;

    const size_t ap_password_len = strlen(CONFIG_SPARKBOT_WEB_CONFIG_AP_PASSWORD);
    if (ap_password_len >= 8) {
        copy_string((char *)ap_config.ap.password, sizeof(ap_config.ap.password),
                    CONFIG_SPARKBOT_WEB_CONFIG_AP_PASSWORD);
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else {
        if (ap_password_len > 0) {
            ESP_LOGW(TAG, "SoftAP password is shorter than 8 characters, use open AP instead");
        }
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set apsta mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "set ap config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    update_ap_ip_string();

    return ESP_OK;
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *message)
{
    char body[160];
    httpd_resp_set_status(req, status);
    snprintf(body, sizeof(body), "{\"ok\":false,\"message\":\"%s\"}", message);
    return send_json(req, body);
}

static esp_err_t send_chunk_str(httpd_req_t *req, const char *text)
{
    return httpd_resp_send_chunk(req, text, text ? strlen(text) : 0);
}

static esp_err_t send_json_escaped(httpd_req_t *req, const char *text)
{
    if (!text) {
        return ESP_OK;
    }

    char escaped[7];
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)*p;
            escaped[2] = '\0';
            ESP_RETURN_ON_ERROR(send_chunk_str(req, escaped), TAG, "send escaped json failed");
        } else if (*p < 0x20) {
            snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
            ESP_RETURN_ON_ERROR(send_chunk_str(req, escaped), TAG, "send escaped json failed");
        } else {
            char one[2] = {(char)*p, '\0'};
            ESP_RETURN_ON_ERROR(send_chunk_str(req, one), TAG, "send json char failed");
        }
    }
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const size_t html_len = (size_t)(index_html_end - index_html_start);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)index_html_start, html_len);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    app_status_snapshot_t snapshot;
    status_get_snapshot(&snapshot);

    int rssi = 0;
    if (snapshot.state == APP_NET_STATE_CONNECTED) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi = ap_info.rssi;
        }
    }

    char chunk[192];
    httpd_resp_set_type(req, "application/json");
    snprintf(chunk, sizeof(chunk), "{\"ok\":true,\"state\":\"%s\",\"configured\":%s,\"ssid\":\"",
             state_to_string(snapshot.state), snapshot.configured ? "true" : "false");
    ESP_RETURN_ON_ERROR(send_chunk_str(req, chunk), TAG, "send status failed");
    ESP_RETURN_ON_ERROR(send_json_escaped(req, snapshot.ssid), TAG, "send ssid failed");
    ESP_RETURN_ON_ERROR(send_chunk_str(req, "\",\"sta_ip\":\""), TAG, "send status failed");
    ESP_RETURN_ON_ERROR(send_json_escaped(req, snapshot.sta_ip), TAG, "send sta ip failed");
    ESP_RETURN_ON_ERROR(send_chunk_str(req, "\",\"ap_ssid\":\""), TAG, "send status failed");
    ESP_RETURN_ON_ERROR(send_json_escaped(req, snapshot.ap_ssid), TAG, "send ap ssid failed");
    snprintf(chunk, sizeof(chunk), "\",\"ap_ip\":\"%s\",\"http_port\":%d,\"rssi\":%d,\"retry\":%d,\"last_error\":\"",
             snapshot.ap_ip, CONFIG_SPARKBOT_WEB_CONFIG_HTTP_PORT, rssi, snapshot.retry);
    ESP_RETURN_ON_ERROR(send_chunk_str(req, chunk), TAG, "send status failed");
    ESP_RETURN_ON_ERROR(send_json_escaped(req, snapshot.last_error), TAG, "send error failed");
    ESP_RETURN_ON_ERROR(send_chunk_str(req, "\"}"), TAG, "send status failed");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    lcd_post(LCD_FACE_SCENE_SURPRISED, "SCAN", "WIFI");

    wifi_ap_record_t *records = calloc(WIFI_SCAN_MAX_AP, sizeof(wifi_ap_record_t));
    if (!records) {
        return send_json_error(req, "500 Internal Server Error", "No memory for scan");
    }

    wifi_scan_config_t scan_config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        free(records);
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return send_json_error(req, "503 Service Unavailable", esp_err_to_name(err));
    }

    uint16_t ap_count = WIFI_SCAN_MAX_AP;
    err = esp_wifi_scan_get_ap_records(&ap_count, records);
    if (err != ESP_OK) {
        free(records);
        return send_json_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    httpd_resp_set_type(req, "application/json");
    ESP_RETURN_ON_ERROR(send_chunk_str(req, "{\"ok\":true,\"aps\":["), TAG, "send scan failed");
    for (uint16_t i = 0; i < ap_count; i++) {
        char chunk[128];
        if (i > 0) {
            ESP_RETURN_ON_ERROR(send_chunk_str(req, ","), TAG, "send scan comma failed");
        }
        ESP_RETURN_ON_ERROR(send_chunk_str(req, "{\"ssid\":\""), TAG, "send scan ssid failed");
        ESP_RETURN_ON_ERROR(send_json_escaped(req, (const char *)records[i].ssid), TAG, "send scan ssid failed");
        snprintf(chunk, sizeof(chunk), "\",\"rssi\":%d,\"auth\":\"%s\",\"channel\":%d}",
                 records[i].rssi, auth_mode_to_string(records[i].authmode), records[i].primary);
        ESP_RETURN_ON_ERROR(send_chunk_str(req, chunk), TAG, "send scan item failed");
    }
    ESP_RETURN_ON_ERROR(send_chunk_str(req, "]}"), TAG, "send scan end failed");
    free(records);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t read_request_body(httpd_req_t *req, char *body, size_t body_len)
{
    if (req->content_len >= body_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }
    body[received] = '\0';
    return ESP_OK;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void url_decode(char *dst, size_t dst_len, const char *src)
{
    size_t out = 0;
    for (size_t i = 0; src[i] && out + 1 < dst_len; i++) {
        if (src[i] == '+') {
            dst[out++] = ' ';
        } else if (src[i] == '%' && src[i + 1] && src[i + 2]) {
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                dst[out++] = src[i];
            }
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

static bool form_get_value(const char *body, const char *key, char *out, size_t out_len)
{
    const size_t key_len = strlen(key);
    const char *p = body;

    while (*p) {
        const char *next = strchr(p, '&');
        const size_t pair_len = next ? (size_t)(next - p) : strlen(p);
        const char *eq = memchr(p, '=', pair_len);

        if (eq && (size_t)(eq - p) == key_len && strncmp(p, key, key_len) == 0) {
            char encoded[HTTP_BODY_MAX_LEN];
            size_t value_len = pair_len - key_len - 1;
            if (value_len >= sizeof(encoded)) {
                value_len = sizeof(encoded) - 1;
            }
            memcpy(encoded, eq + 1, value_len);
            encoded[value_len] = '\0';
            url_decode(out, out_len, encoded);
            return true;
        }

        if (!next) {
            break;
        }
        p = next + 1;
    }

    return false;
}

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    char body[HTTP_BODY_MAX_LEN];
    esp_err_t err = read_request_body(req, body, sizeof(body));
    if (err == ESP_ERR_INVALID_SIZE) {
        return send_json_error(req, "413 Payload Too Large", "Request body too large");
    }
    if (err != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "Cannot read request body");
    }

    char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
    char password[WIFI_PASSWORD_MAX_LEN + 1] = {0};

    if (!form_get_value(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        return send_json_error(req, "400 Bad Request", "SSID is required");
    }
    form_get_value(body, "password", password, sizeof(password));

    const size_t password_len = strlen(password);
    if (password_len > 0 && password_len < 8) {
        return send_json_error(req, "400 Bad Request", "Password must be empty or at least 8 chars");
    }

    err = start_sta_connect(ssid, password, true);
    memset(password, 0, sizeof(password));
    memset(body, 0, sizeof(body));

    if (err != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    return send_json(req, "{\"ok\":true,\"message\":\"connecting\"}");
}

static esp_err_t forget_post_handler(httpd_req_t *req)
{
    esp_err_t err = nvs_forget_wifi();
    if (err != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_has_credentials = false;
    s_ignore_next_disconnect = true;
    s_retry_count = 0;
    copy_string(s_current_ssid, sizeof(s_current_ssid), "");
    copy_string(s_sta_ip, sizeof(s_sta_ip), "0.0.0.0");
    copy_string(s_last_error, sizeof(s_last_error), "Forgot WiFi");
    s_net_state = APP_NET_STATE_CONFIG;
    xSemaphoreGive(s_status_lock);

    esp_err_t disconnect_err = esp_wifi_disconnect();
    if (disconnect_err == ESP_ERR_WIFI_NOT_CONNECT) {
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        s_ignore_next_disconnect = false;
        xSemaphoreGive(s_status_lock);
    }

    lcd_post(LCD_FACE_SCENE_IDLE, "CONFIG", "FORGOT WIFI");
    return send_json(req, "{\"ok\":true,\"message\":\"forgot\"}");
}

static esp_err_t http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_SPARKBOT_WEB_CONFIG_HTTP_PORT;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;

    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &config), TAG, "httpd start failed");

    const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    const httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
    };
    const httpd_uri_t scan_uri = {
        .uri = "/api/scan",
        .method = HTTP_GET,
        .handler = scan_get_handler,
    };
    const httpd_uri_t wifi_uri = {
        .uri = "/api/wifi",
        .method = HTTP_POST,
        .handler = wifi_post_handler,
    };
    const httpd_uri_t forget_uri = {
        .uri = "/api/forget",
        .method = HTTP_POST,
        .handler = forget_post_handler,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &root_uri), TAG, "register root failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &status_uri), TAG, "register status failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &scan_uri), TAG, "register scan failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &wifi_uri), TAG, "register wifi failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &forget_uri), TAG, "register forget failed");

    ESP_LOGI(TAG, "HTTP server started on port %d", CONFIG_SPARKBOT_WEB_CONFIG_HTTP_PORT);
    return ESP_OK;
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        err = nvs_flash_init();
    }
    return err;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-SparkBot LCD WiFi web config starting");

    ESP_ERROR_CHECK(init_nvs());

    s_status_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_status_lock ? ESP_OK : ESP_ERR_NO_MEM);

    s_lcd_queue = xQueueCreate(6, sizeof(lcd_status_msg_t));
    ESP_ERROR_CHECK(s_lcd_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(lcd_status_task, "lcd_status", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_FAIL);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(wifi_start_apsta());
    ESP_ERROR_CHECK(http_server_start());

    char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
    char password[WIFI_PASSWORD_MAX_LEN + 1] = {0};
    esp_err_t err = nvs_load_wifi(ssid, sizeof(ssid), password, sizeof(password));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded WiFi SSID from NVS: %s", ssid);
        ESP_ERROR_CHECK_WITHOUT_ABORT(start_sta_connect(ssid, password, false));
    } else {
        ESP_LOGI(TAG, "No saved WiFi credentials, stay in config mode");
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        s_has_credentials = false;
        xSemaphoreGive(s_status_lock);
        set_config_state("No WiFi saved");
    }
    memset(password, 0, sizeof(password));

    bool show_web_prompt = true;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(4000));

        app_status_snapshot_t snapshot;
        status_get_snapshot(&snapshot);
        if (snapshot.state == APP_NET_STATE_CONFIG) {
            if (show_web_prompt) {
                lcd_post_web_prompt();
            } else {
                lcd_post_config_prompt();
            }
            show_web_prompt = !show_web_prompt;
        } else {
            show_web_prompt = true;
        }
    }
}
