#include "weather_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAXIMUM_RETRY 10
#define WEATHER_HTTP_TIMEOUT_MS 8000
#define WEATHER_HTTP_BUFFER_HINT 1536

static const char *TAG = "weather_service";

typedef struct {
    char *data;
    int len;
    int cap;
} http_response_buffer_t;

static EventGroupHandle_t s_wifi_event_group;
static weather_wifi_status_t s_wifi_status = WEATHER_WIFI_IDLE;
static int s_retry_num;
static bool s_wifi_started;

void weather_report_set_message(weather_report_t *report, const char *message, esp_err_t err)
{
    if (!report) {
        return;
    }

    report->valid = false;
    report->wifi_connected = s_wifi_status == WEATHER_WIFI_CONNECTED;
    report->last_error = err;
    snprintf(report->location, sizeof(report->location), "%s", CONFIG_WEATHER_LOCATION);
    snprintf(report->message, sizeof(report->message), "%s", message ? message : "No message");
}

bool weather_service_has_required_config(void)
{
    return CONFIG_WEATHER_WIFI_SSID[0] != '\0' && CONFIG_WEATHER_QWEATHER_KEY[0] != '\0';
}

const char *weather_service_configured_ssid(void)
{
    return CONFIG_WEATHER_WIFI_SSID;
}

const char *weather_service_configured_location(void)
{
    return CONFIG_WEATHER_LOCATION;
}

weather_wifi_status_t weather_service_wifi_status(void)
{
    return s_wifi_status;
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_status = WEATHER_WIFI_CONNECTING;
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            s_retry_num++;
            s_wifi_status = WEATHER_WIFI_CONNECTING;
            esp_wifi_connect();
            ESP_LOGW(TAG, "Wi-Fi disconnected, retry %d/%d", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            s_wifi_status = WEATHER_WIFI_FAILED;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_wifi_status = WEATHER_WIFI_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t weather_service_init(void)
{
    if (s_wifi_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(CONFIG_WEATHER_WIFI_SSID[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "Wi-Fi SSID is empty");

    s_wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_event_group, ESP_ERR_NO_MEM, TAG, "create Wi-Fi event group failed");

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "esp_netif_init failed");
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "create default event loop failed");
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            wifi_event_handler,
                                                            NULL,
                                                            NULL),
                        TAG,
                        "register Wi-Fi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            wifi_event_handler,
                                                            NULL,
                                                            NULL),
                        TAG,
                        "register IP event handler failed");

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", CONFIG_WEATHER_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", CONFIG_WEATHER_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set Wi-Fi STA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set Wi-Fi config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi failed");

    s_wifi_started = true;
    return ESP_OK;
}

esp_err_t weather_service_connect(void)
{
    ESP_RETURN_ON_ERROR(weather_service_init(), TAG, "init weather service failed");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(15000));
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;
    }

    s_wifi_status = WEATHER_WIFI_FAILED;
    return ESP_ERR_TIMEOUT;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_buffer_t *buffer = (http_response_buffer_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
        if (buffer) {
            buffer->len = 0;
        }
        break;

    case HTTP_EVENT_ON_DATA:
        if (!buffer || !evt->data || evt->data_len <= 0) {
            break;
        }

        if (buffer->len + evt->data_len + 1 > buffer->cap) {
            int new_cap = buffer->cap ? buffer->cap * 2 : WEATHER_HTTP_BUFFER_HINT;
            while (new_cap < buffer->len + evt->data_len + 1) {
                new_cap *= 2;
            }
            char *new_data = realloc(buffer->data, new_cap);
            if (!new_data) {
                ESP_LOGE(TAG, "HTTP response realloc failed");
                return ESP_ERR_NO_MEM;
            }
            buffer->data = new_data;
            buffer->cap = new_cap;
        }

        memcpy(buffer->data + buffer->len, evt->data, evt->data_len);
        buffer->len += evt->data_len;
        buffer->data[buffer->len] = '\0';
        break;

    default:
        break;
    }

    return ESP_OK;
}

static esp_err_t parse_weather_response(const char *json_text, weather_report_t *report)
{
    cJSON *root = cJSON_Parse(json_text);
    ESP_RETURN_ON_FALSE(root, ESP_FAIL, TAG, "parse QWeather JSON failed");

    esp_err_t ret = ESP_OK;
    cJSON *code = cJSON_GetObjectItem(root, "code");
    cJSON *now = cJSON_GetObjectItem(root, "now");
    if (!cJSON_IsString(code) || strcmp(code->valuestring, "200") != 0 || !cJSON_IsObject(now)) {
        const char *api_code = cJSON_IsString(code) ? code->valuestring : "unknown";
        snprintf(report->message, sizeof(report->message), "QWeather API code %s", api_code);
        ret = ESP_FAIL;
        goto done;
    }

    cJSON *temp = cJSON_GetObjectItem(now, "temp");
    cJSON *icon = cJSON_GetObjectItem(now, "icon");
    cJSON *text = cJSON_GetObjectItem(now, "text");
    cJSON *obs_time = cJSON_GetObjectItem(now, "obsTime");

    ESP_GOTO_ON_FALSE(cJSON_IsString(temp) &&
                          cJSON_IsString(icon) &&
                          cJSON_IsString(text),
                      ESP_FAIL,
                      done,
                      TAG,
                      "QWeather JSON missing now.temp/icon/text");

    memset(report, 0, sizeof(*report));
    report->valid = true;
    report->wifi_connected = s_wifi_status == WEATHER_WIFI_CONNECTED;
    report->last_error = ESP_OK;
    snprintf(report->location, sizeof(report->location), "%s", CONFIG_WEATHER_LOCATION);
    snprintf(report->temp_c, sizeof(report->temp_c), "%s", temp->valuestring);
    snprintf(report->condition, sizeof(report->condition), "%s", text->valuestring);
    snprintf(report->icon, sizeof(report->icon), "%s", icon->valuestring);
    snprintf(report->obs_time, sizeof(report->obs_time), "%s",
             cJSON_IsString(obs_time) ? obs_time->valuestring : "--");
    snprintf(report->message, sizeof(report->message), "Weather updated");

done:
    cJSON_Delete(root);
    return ret;
}

esp_err_t weather_service_fetch(weather_report_t *report)
{
    ESP_RETURN_ON_FALSE(report, ESP_ERR_INVALID_ARG, TAG, "report is NULL");

    if (CONFIG_WEATHER_QWEATHER_KEY[0] == '\0') {
        weather_report_set_message(report, "Set QWeather key in menuconfig", ESP_ERR_INVALID_ARG);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_wifi_status != WEATHER_WIFI_CONNECTED) {
        esp_err_t connect_ret = weather_service_connect();
        if (connect_ret != ESP_OK) {
            weather_report_set_message(report, "Wi-Fi connection failed", connect_ret);
            return connect_ret;
        }
    }

    char url[256];
    snprintf(url,
             sizeof(url),
             "https://devapi.qweather.com/v7/weather/now?location=%s&lang=%s&gzip=n&key=%s",
             CONFIG_WEATHER_LOCATION,
             CONFIG_WEATHER_LANGUAGE,
             CONFIG_WEATHER_QWEATHER_KEY);

    http_response_buffer_t response = {0};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_RETURN_ON_FALSE(client, ESP_FAIL, TAG, "init HTTP client failed");
    esp_http_client_set_header(client, "User-Agent", "esp-sparkbot-weather");
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Cache-Control", "no-cache");

    esp_err_t ret = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    if (ret != ESP_OK || status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "QWeather request failed: %s, status=%d", esp_err_to_name(ret), status_code);
        weather_report_set_message(report, "Weather request failed", ret != ESP_OK ? ret : ESP_FAIL);
        goto cleanup;
    }

    ret = parse_weather_response(response.data, report);
    if (ret != ESP_OK) {
        report->valid = false;
        report->wifi_connected = true;
        report->last_error = ret;
        if (report->message[0] == '\0') {
            snprintf(report->message, sizeof(report->message), "Weather parse failed");
        }
        goto cleanup;
    }

    ESP_LOGI(TAG, "Weather: %s C, %s, icon %s", report->temp_c, report->condition, report->icon);

cleanup:
    esp_http_client_cleanup(client);
    free(response.data);
    return ret;
}
