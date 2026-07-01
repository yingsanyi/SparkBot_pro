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
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lcd_face_ui.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "tracked_chassis_control.h"

#define FORM_BODY_MAX_LEN           128
#define WS_TEXT_MAX_LEN             64
#define WIFI_SSID_FIELD_LEN         32
#define WIFI_AP_MAX_CLIENTS         4
#define WIFI_AP_SUFFIX_CHARS        13

typedef struct {
    float x;
    float y;
    int light_mode;
    int dance_mode;
    int ap_clients;
    TickType_t last_motion_tick;
    uint32_t motion_seq;
    char last_command[32];
    char ap_ssid[WIFI_SSID_FIELD_LEN + 1];
    char ap_ip[16];
} app_state_t;

typedef struct {
    lcd_face_scene_t scene;
    char line1[24];
    char line2[48];
} lcd_status_msg_t;

extern const unsigned char index_html_start[] asm("_binary_index_html_start");
extern const unsigned char index_html_end[] asm("_binary_index_html_end");

static const char *TAG = "web_car_control";

static SemaphoreHandle_t s_state_lock;
static QueueHandle_t s_lcd_queue;
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_httpd;
static app_state_t s_state = {
    .ap_ip = "192.168.4.1",
    .last_command = "x0.00 y0.00",
};

static float clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static bool motion_active(float x, float y)
{
    return x > 0.01f || x < -0.01f || y > 0.01f || y < -0.01f;
}

static void format_fixed_2(char *dst, size_t dst_len, float value)
{
    int scaled = value >= 0.0f ? (int)(value * 100.0f + 0.5f) : (int)(value * 100.0f - 0.5f);
    const char *sign = "";
    if (scaled < 0) {
        sign = "-";
        scaled = -scaled;
    }
    snprintf(dst, dst_len, "%s%d.%02d", sign, scaled / 100, scaled % 100);
}

static void format_motion_command(char *dst, size_t dst_len, float x, float y)
{
    char x_text[12];
    char y_text[12];
    format_fixed_2(x_text, sizeof(x_text), x);
    format_fixed_2(y_text, sizeof(y_text), y);
    snprintf(dst, dst_len, "x%s y%s", x_text, y_text);
}

static void copy_string(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0) {
        return;
    }
    snprintf(dst, dst_len, "%s", src ? src : "");
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
        ESP_LOGW(TAG, "LCD queue full, drop %s/%s", msg.line1, msg.line2);
    }
}

static void lcd_post_config_prompt(void)
{
    char ap_ssid[WIFI_SSID_FIELD_LEN + 1] = {0};

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    copy_string(ap_ssid, sizeof(ap_ssid), s_state.ap_ssid);
    xSemaphoreGive(s_state_lock);

    lcd_post(LCD_FACE_SCENE_IDLE, "JOIN AP", ap_ssid[0] ? ap_ssid : "CAR AP");
}

static void lcd_post_web_prompt(void)
{
    char ap_ip[16] = {0};
    char web_addr[24] = {0};

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    copy_string(ap_ip, sizeof(ap_ip), s_state.ap_ip);
    xSemaphoreGive(s_state_lock);

    if (ap_ip[0] == '\0' || strcmp(ap_ip, "0.0.0.0") == 0) {
        copy_string(ap_ip, sizeof(ap_ip), "192.168.4.1");
    }

    if (CONFIG_SPARKBOT_WEB_CAR_HTTP_PORT == 80) {
        copy_string(web_addr, sizeof(web_addr), ap_ip);
    } else {
        snprintf(web_addr, sizeof(web_addr), "%s:%d", ap_ip, CONFIG_SPARKBOT_WEB_CAR_HTTP_PORT);
    }

    lcd_post(LCD_FACE_SCENE_SURPRISED, web_addr, "OPEN WEB");
}

static void lcd_status_task(void *arg)
{
    (void)arg;

    ESP_ERROR_CHECK(lcd_face_ui_init());
    lcd_face_ui_show_scene(LCD_FACE_SCENE_BOOT, "WEB CAR", "BOOT");

    lcd_status_msg_t msg;
    while (true) {
        if (xQueueReceive(s_lcd_queue, &msg, portMAX_DELAY) == pdTRUE) {
            lcd_face_ui_show_scene(msg.scene, msg.line1, msg.line2);
        }
    }
}

static size_t copy_wifi_field(uint8_t *dst, size_t dst_len, const char *src)
{
    memset(dst, 0, dst_len);
    if (!src) {
        return 0;
    }

    size_t src_len = strlen(src);
    if (src_len > dst_len) {
        src_len = dst_len;
    }
    memcpy(dst, src, src_len);
    return src_len;
}
static void state_set_motion(float x, float y)
{
    x = clamp_float(x, -1.0f, 1.0f);
    y = clamp_float(y, -1.0f, 1.0f);
    const bool next_active = motion_active(x, y);
    bool motion_state_changed = false;

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    const bool was_active = motion_active(s_state.x, s_state.y);
    s_state.x = x;
    s_state.y = y;
    s_state.last_motion_tick = xTaskGetTickCount();
    s_state.motion_seq++;
    format_motion_command(s_state.last_command, sizeof(s_state.last_command), x, y);
    motion_state_changed = was_active != next_active;
    xSemaphoreGive(s_state_lock);

    ESP_ERROR_CHECK_WITHOUT_ABORT(tracked_chassis_send_motion(x, y));
    if (motion_state_changed) {
        lcd_post(next_active ? LCD_FACE_SCENE_WINK : LCD_FACE_SCENE_IDLE,
                 next_active ? "MOVING" : "READY",
                 next_active ? "WEB CONTROL" : "OPEN WEB");
    }
}

static void state_stop_motion(void)
{
    state_set_motion(0.0f, 0.0f);
}

static void state_set_light(int mode)
{
    if (mode < 0) {
        mode = 0;
    }
    if (mode > 9) {
        mode = 9;
    }

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_state.light_mode = mode;
    snprintf(s_state.last_command, sizeof(s_state.last_command), "w%d", mode);
    xSemaphoreGive(s_state_lock);

    ESP_ERROR_CHECK_WITHOUT_ABORT(tracked_chassis_rgb_light_control((uint8_t)mode));
    char line2[24];
    snprintf(line2, sizeof(line2), "MODE %d", mode);
    lcd_post(LCD_FACE_SCENE_HAPPY, "LIGHT", line2);
}

static void state_set_dance(int mode)
{
    if (mode < 0) {
        mode = 0;
    }
    if (mode > 9) {
        mode = 9;
    }

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_state.dance_mode = mode;
    snprintf(s_state.last_command, sizeof(s_state.last_command), "d%d", mode);
    xSemaphoreGive(s_state_lock);

    ESP_ERROR_CHECK_WITHOUT_ABORT(tracked_chassis_set_dance_mode((uint8_t)mode));
    lcd_post(LCD_FACE_SCENE_SURPRISED, "DANCE", "CHASSIS");
}

static void state_set_correction(float correction)
{
    correction = clamp_float(correction, -1.0f, 1.0f);
    char correction_text[12];
    format_fixed_2(correction_text, sizeof(correction_text), correction);

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    snprintf(s_state.last_command, sizeof(s_state.last_command), "c%s", correction_text);
    xSemaphoreGive(s_state_lock);

    ESP_ERROR_CHECK_WITHOUT_ABORT(tracked_chassis_set_correction(correction));
    lcd_post(LCD_FACE_SCENE_IDLE, "TRIM", correction < 0.0f ? "LEFT" : "RIGHT");
}

static void motion_keepalive_task(void *arg)
{
    (void)arg;

    while (true) {
        float x = 0.0f;
        float y = 0.0f;
        bool should_repeat = false;
        bool should_stop = false;

        xSemaphoreTake(s_state_lock, portMAX_DELAY);
        const TickType_t now = xTaskGetTickCount();
        const TickType_t elapsed = now - s_state.last_motion_tick;
        const bool active = motion_active(s_state.x, s_state.y);

        if (active && elapsed > pdMS_TO_TICKS(CONFIG_SPARKBOT_WEB_CAR_DEADMAN_MS)) {
            s_state.x = 0.0f;
            s_state.y = 0.0f;
            s_state.motion_seq++;
            format_motion_command(s_state.last_command, sizeof(s_state.last_command), 0.0f, 0.0f);
            should_stop = true;
        } else if (active) {
            should_repeat = true;
        }

        x = s_state.x;
        y = s_state.y;
        xSemaphoreGive(s_state_lock);

        if (should_stop) {
            ESP_LOGW(TAG, "Motion command timeout, stop chassis");
            ESP_ERROR_CHECK_WITHOUT_ABORT(tracked_chassis_send_motion(0.0f, 0.0f));
            lcd_post(LCD_FACE_SCENE_ANGRY, "STOP", "TIMEOUT");
        } else if (should_repeat) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(tracked_chassis_send_motion(x, y));
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_SPARKBOT_WEB_CAR_COMMAND_PERIOD_MS));
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

        xSemaphoreTake(s_state_lock, portMAX_DELAY);
        copy_string(s_state.ap_ip, sizeof(s_state.ap_ip), ip_buf);
        xSemaphoreGive(s_state_lock);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_AP_START) {
        update_ap_ip_string();
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
        s_state.ap_clients = 0;
        xSemaphoreGive(s_state_lock);
        ESP_LOGI(TAG, "Car AP started: %s, http://%s", s_state.ap_ssid, s_state.ap_ip);
        lcd_post_config_prompt();
        return;
    }

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
        if (s_state.ap_clients < WIFI_AP_MAX_CLIENTS) {
            s_state.ap_clients++;
        }
        const int clients = s_state.ap_clients;
        xSemaphoreGive(s_state_lock);
        ESP_LOGI(TAG, "Browser device joined AP, clients=%d", clients);
        lcd_post_web_prompt();
        return;
    }

    if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        xSemaphoreTake(s_state_lock, portMAX_DELAY);
        if (s_state.ap_clients > 0) {
            s_state.ap_clients--;
        }
        const int clients = s_state.ap_clients;
        xSemaphoreGive(s_state_lock);
        state_stop_motion();
        ESP_LOGI(TAG, "Browser device left AP, clients=%d", clients);
        if (clients > 0) {
            lcd_post_web_prompt();
        } else {
            lcd_post_config_prompt();
        }
    }
}

static void build_ap_ssid(char *ssid, size_t ssid_len)
{
    if (ssid_len == 0) {
        return;
    }

    const char *base = CONFIG_SPARKBOT_WEB_CAR_AP_SSID_PREFIX;
    if (!base || base[0] == '\0') {
        base = "SparkBot-Car";
    }

    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Read SoftAP MAC failed: %s, use base AP SSID", esp_err_to_name(err));
        copy_string(ssid, ssid_len, base);
        return;
    }

    const size_t max_ssid_chars = ssid_len - 1;
    const size_t max_base_len = max_ssid_chars > WIFI_AP_SUFFIX_CHARS ? max_ssid_chars - WIFI_AP_SUFFIX_CHARS : 0;
    size_t base_len = strlen(base);
    if (base_len > max_base_len) {
        base_len = max_base_len;
    }

    snprintf(ssid, ssid_len, "%.*s-%02X%02X%02X%02X%02X%02X",
             (int)base_len, base, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    copy_string(s_state.ap_ssid, sizeof(s_state.ap_ssid), ssid);
    xSemaphoreGive(s_state_lock);
}
static esp_err_t wifi_start_softap(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "create event loop failed");

    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_ap_netif, ESP_FAIL, TAG, "create AP netif failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL),
                        TAG, "register wifi event failed");

    char ap_ssid[WIFI_SSID_FIELD_LEN + 1] = {0};
    build_ap_ssid(ap_ssid, sizeof(ap_ssid));

    wifi_config_t ap_config = {0};
    ap_config.ap.ssid_len = copy_wifi_field(ap_config.ap.ssid, sizeof(ap_config.ap.ssid), ap_ssid);
    ap_config.ap.channel = CONFIG_SPARKBOT_WEB_CAR_AP_CHANNEL;
    ap_config.ap.max_connection = WIFI_AP_MAX_CLIENTS;
    ap_config.ap.pmf_cfg.required = false;

    const size_t password_len = strlen(CONFIG_SPARKBOT_WEB_CAR_AP_PASSWORD);
    if (password_len >= 8) {
        copy_wifi_field(ap_config.ap.password, sizeof(ap_config.ap.password), CONFIG_SPARKBOT_WEB_CAR_AP_PASSWORD);
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else {
        if (password_len > 0) {
            ESP_LOGW(TAG, "AP password shorter than 8 chars, use open AP");
        }
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "set AP config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    update_ap_ip_string();

    ESP_LOGI(TAG, "Join SSID %s, then open http://%s", ap_ssid, s_state.ap_ip);
    return ESP_OK;
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *message)
{
    char body[160];
    httpd_resp_set_status(req, status);
    snprintf(body, sizeof(body), "{\"ok\":false,\"message\":\"%s\"}", message);
    return send_json(req, body);
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
    app_state_t snapshot;

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    snapshot = s_state;
    xSemaphoreGive(s_state_lock);

    char x_text[12];
    char y_text[12];
    format_fixed_2(x_text, sizeof(x_text), snapshot.x);
    format_fixed_2(y_text, sizeof(y_text), snapshot.y);

    char body[448];
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"http_port\":%d,\"clients\":%d,"
             "\"x\":%s,\"y\":%s,\"moving\":%s,\"light_mode\":%d,\"dance_mode\":%d,"
             "\"motion_seq\":%lu,\"last_command\":\"%s\"}",
             snapshot.ap_ssid,
             snapshot.ap_ip,
             CONFIG_SPARKBOT_WEB_CAR_HTTP_PORT,
             snapshot.ap_clients,
             x_text,
             y_text,
             motion_active(snapshot.x, snapshot.y) ? "true" : "false",
             snapshot.light_mode,
             snapshot.dance_mode,
             (unsigned long)snapshot.motion_seq,
             snapshot.last_command);

    return send_json(req, body);
}

static esp_err_t read_request_body(httpd_req_t *req, char *body, size_t body_len)
{
    if (req->content_len >= body_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t received = 0;
    while (received < req->content_len) {
        const int ret = httpd_req_recv(req, body + received, req->content_len - received);
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
            const int hi = hex_value(src[i + 1]);
            const int lo = hex_value(src[i + 2]);
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
            char encoded[FORM_BODY_MAX_LEN];
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
static bool form_get_float(const char *body, const char *key, float *out)
{
    char value[24];
    char *end = NULL;

    if (!form_get_value(body, key, value, sizeof(value))) {
        return false;
    }

    const float parsed = strtof(value, &end);
    if (end == value) {
        return false;
    }

    *out = parsed;
    return true;
}

static bool form_get_int(const char *body, const char *key, int *out)
{
    char value[16];
    char *end = NULL;

    if (!form_get_value(body, key, value, sizeof(value))) {
        return false;
    }

    const long parsed = strtol(value, &end, 10);
    if (end == value) {
        return false;
    }

    *out = (int)parsed;
    return true;
}

static esp_err_t move_post_handler(httpd_req_t *req)
{
    char body[FORM_BODY_MAX_LEN];
    esp_err_t err = read_request_body(req, body, sizeof(body));
    if (err == ESP_ERR_INVALID_SIZE) {
        return send_json_error(req, "413 Payload Too Large", "request body too large");
    }
    if (err != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "cannot read request body");
    }

    float x = 0.0f;
    float y = 0.0f;
    int speed = 100;

    if (!form_get_float(body, "x", &x) || !form_get_float(body, "y", &y)) {
        return send_json_error(req, "400 Bad Request", "x and y are required");
    }
    form_get_int(body, "speed", &speed);
    if (speed < 0) {
        speed = 0;
    }
    if (speed > 100) {
        speed = 100;
    }

    const float scale = (float)speed / 100.0f;
    state_set_motion(x * scale, y * scale);
    return send_json(req, "{\"ok\":true,\"message\":\"moving\"}");
}

static esp_err_t stop_post_handler(httpd_req_t *req)
{
    (void)req;
    state_stop_motion();
    return send_json(req, "{\"ok\":true,\"message\":\"stopped\"}");
}

static esp_err_t light_post_handler(httpd_req_t *req)
{
    char body[FORM_BODY_MAX_LEN];
    int mode = 0;

    esp_err_t err = read_request_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "cannot read request body");
    }
    if (!form_get_int(body, "mode", &mode)) {
        return send_json_error(req, "400 Bad Request", "mode is required");
    }

    state_set_light(mode);
    return send_json(req, "{\"ok\":true,\"message\":\"light changed\"}");
}

static esp_err_t dance_post_handler(httpd_req_t *req)
{
    char body[FORM_BODY_MAX_LEN];
    int mode = 1;

    esp_err_t err = read_request_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "cannot read request body");
    }
    form_get_int(body, "mode", &mode);

    state_set_dance(mode);
    return send_json(req, "{\"ok\":true,\"message\":\"dance sent\"}");
}

static esp_err_t correction_post_handler(httpd_req_t *req)
{
    char body[FORM_BODY_MAX_LEN];
    float value = 0.0f;

    esp_err_t err = read_request_body(req, body, sizeof(body));
    if (err != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "cannot read request body");
    }
    if (!form_get_float(body, "value", &value)) {
        return send_json_error(req, "400 Bad Request", "value is required");
    }

    state_set_correction(value);
    return send_json(req, "{\"ok\":true,\"message\":\"correction sent\"}");
}

static void handle_ws_text(const char *text)
{
    if (!text || !text[0]) {
        return;
    }

    if (strcmp(text, "PING") == 0 || strcmp(text, "ping") == 0) {
        return;
    }

    if (strcmp(text, "stop") == 0 || strcmp(text, "STOP") == 0) {
        state_stop_motion();
        return;
    }

    float x = 0.0f;
    float y = 0.0f;
    if (sscanf(text, "x%f y%f", &x, &y) == 2) {
        state_set_motion(x, y);
        return;
    }

    int mode = 0;
    if (sscanf(text, "w%d", &mode) == 1) {
        state_set_light(mode);
        return;
    }

    if (sscanf(text, "d%d", &mode) == 1) {
        state_set_dance(mode);
        return;
    }

    float correction = 0.0f;
    if (sscanf(text, "c%f", &correction) == 1) {
        state_set_correction(correction);
        return;
    }

    ESP_LOGW(TAG, "Ignore unknown WS command: %s", text);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket connected");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
    };

    esp_err_t err = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ws recv frame length failed: %s", esp_err_to_name(err));
        return err;
    }

    if (ws_pkt.len == 0) {
        return ESP_OK;
    }
    if (ws_pkt.len >= WS_TEXT_MAX_LEN) {
        ESP_LOGW(TAG, "ws frame too long: %d", ws_pkt.len);
        return ESP_ERR_INVALID_SIZE;
    }

    char *buf = calloc(1, ws_pkt.len + 1);
    ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "no memory for ws frame");
    ws_pkt.payload = (uint8_t *)buf;

    err = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (err == ESP_OK && ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        handle_ws_text(buf);
    }

    free(buf);
    return err;
}
static esp_err_t http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_SPARKBOT_WEB_CAR_HTTP_PORT;
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

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
    const httpd_uri_t move_uri = {
        .uri = "/api/move",
        .method = HTTP_POST,
        .handler = move_post_handler,
    };
    const httpd_uri_t stop_uri = {
        .uri = "/api/stop",
        .method = HTTP_POST,
        .handler = stop_post_handler,
    };
    const httpd_uri_t light_uri = {
        .uri = "/api/light",
        .method = HTTP_POST,
        .handler = light_post_handler,
    };
    const httpd_uri_t dance_uri = {
        .uri = "/api/dance",
        .method = HTTP_POST,
        .handler = dance_post_handler,
    };
    const httpd_uri_t correction_uri = {
        .uri = "/api/correction",
        .method = HTTP_POST,
        .handler = correction_post_handler,
    };
    const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &root_uri), TAG, "register / failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &status_uri), TAG, "register status failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &move_uri), TAG, "register move failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &stop_uri), TAG, "register stop failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &light_uri), TAG, "register light failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &dance_uri), TAG, "register dance failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &correction_uri), TAG, "register correction failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &ws_uri), TAG, "register websocket failed");

    ESP_LOGI(TAG, "HTTP server started on http://%s:%d", s_state.ap_ip, config.server_port);
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
    ESP_LOGI(TAG, "ESP-SparkBot web car control starting");

    ESP_ERROR_CHECK(init_nvs());

    s_state_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_state_lock ? ESP_OK : ESP_ERR_NO_MEM);

    s_lcd_queue = xQueueCreate(4, sizeof(lcd_status_msg_t));
    ESP_ERROR_CHECK(s_lcd_queue ? ESP_OK : ESP_ERR_NO_MEM);
    xTaskCreate(lcd_status_task, "lcd_status", 4096, NULL, 4, NULL);

    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_state.last_motion_tick = xTaskGetTickCount();
    xSemaphoreGive(s_state_lock);

    ESP_ERROR_CHECK(tracked_chassis_control_start());
    ESP_ERROR_CHECK(wifi_start_softap());
    ESP_ERROR_CHECK(http_server_start());

    xTaskCreate(motion_keepalive_task, "motion_keepalive", 4096, NULL, 5, NULL);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
