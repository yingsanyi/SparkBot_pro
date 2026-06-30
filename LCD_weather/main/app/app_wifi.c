/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "app_wifi.h"
#include "app_sntp.h"
#include "app_weather.h"
#include "time.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
#include "psa/crypto.h"
#endif
#include "esp_crt_bundle.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

#define EXAMPLE_ESP_MAXIMUM_RETRY  10

#define CONFIG_ESP_WIFI_AUTH_WPA2_PSK   1
#define CONFIG_ESP_WPA3_SAE_PWE_BOTH    1
#define CONFIG_ESP_WIFI_PW_ID          ""

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1

#define portTICK_RATE_MS        10

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "restapi.amap.com"
#define WEB_PORT "443"
#define WEB_URL "https://restapi.amap.com/v3/ip?key=6aefab4f1fd2749d0aac1e9f16e22514"
#define DEFAULT_WEATHER_LOCATION "101020100"
#define WEATHER_UPDATE_PERIOD_MS (30 * 60 * 1000)

static const char *TAG = "wifi station";
static int s_retry_num = 0;
static bool s_reconnect = true;
//locaiton longtitude(west) and latitude(south)
static char west_south[20] = DEFAULT_WEATHER_LOCATION;
char current_region[APP_LOCATION_TEXT_MAX_LEN] = "Area --";

static const char *REQUEST = "GET " WEB_URL " HTTP/1.0\r\n"
                             "Host: "WEB_SERVER"\r\n"
                             "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36 Edg/130.0.0.0\r\n"
                             "\r\n";

bool wifi_connected = false;
static QueueHandle_t wifi_event_queue = NULL;
static bool s_network_handlers_registered = false;

typedef struct {
    const char *raw;
    const char *display;
} region_alias_t;

static const region_alias_t REGION_ALIASES[] = {
    {"厦门市", "Xiamen"},
    {"厦门", "Xiamen"},
    {"福州市", "Fuzhou"},
    {"泉州市", "Quanzhou"},
    {"漳州市", "Zhangzhou"},
    {"福建省", "Fujian"},
    {"北京市", "Beijing"},
    {"上海市", "Shanghai"},
    {"天津市", "Tianjin"},
    {"重庆市", "Chongqing"},
    {"广州市", "Guangzhou"},
    {"深圳市", "Shenzhen"},
    {"杭州市", "Hangzhou"},
    {"南京市", "Nanjing"},
    {"苏州市", "Suzhou"},
    {"成都市", "Chengdu"},
    {"武汉市", "Wuhan"},
    {"西安市", "Xi'an"},
};

static const char *get_region_display_name(const char *raw_region)
{
    if (raw_region == NULL || raw_region[0] == '\0') {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(REGION_ALIASES) / sizeof(REGION_ALIASES[0]); i++) {
        if (strcmp(raw_region, REGION_ALIASES[i].raw) == 0) {
            return REGION_ALIASES[i].display;
        }
    }

    return NULL;
}

static bool text_is_ascii(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }

    while (*text) {
        if ((unsigned char)*text > 0x7f) {
            return false;
        }
        text++;
    }

    return true;
}

static void update_current_region(const char *raw_region, const char *fallback_location)
{
    const char *display_region = get_region_display_name(raw_region);

    if (display_region != NULL) {
        snprintf(current_region, sizeof(current_region), "Area %s", display_region);
    } else if (text_is_ascii(raw_region)) {
        snprintf(current_region, sizeof(current_region), "Area %s", raw_region);
    } else if (fallback_location != NULL && fallback_location[0] != '\0') {
        snprintf(current_region, sizeof(current_region), "Area %s", fallback_location);
    } else {
        snprintf(current_region, sizeof(current_region), "Area --");
    }

    ESP_LOGI(TAG, "Current region raw: %s, display: %s",
             raw_region ? raw_region : "--", current_region);
}

static esp_err_t parse_location_data(const char *buffer)
{
    if (buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *header_end = strstr(buffer, "\r\n\r\n");
    if (header_end) {
        header_end += strlen("\r\n\r\n");
        cJSON *json = cJSON_Parse(header_end);
        if (NULL != json) {
            cJSON *city = cJSON_GetObjectItem(json, "city");
            cJSON *province = cJSON_GetObjectItem(json, "province");
            const char *raw_region = NULL;
            if (cJSON_IsString(city) && city->valuestring && city->valuestring[0]) {
                raw_region = city->valuestring;
            } else if (cJSON_IsString(province) && province->valuestring && province->valuestring[0]) {
                raw_region = province->valuestring;
            }

            cJSON *rectangle = cJSON_GetObjectItem(json, "rectangle");
            if (!cJSON_IsString(rectangle) || rectangle->valuestring == NULL) {
                ESP_LOGE(TAG, "Missing rectangle in location response");
                cJSON_Delete(json);
                return ESP_FAIL;
            }
            char *rectangle_str = rectangle->valuestring;
            char *semicolon_pos = strchr(rectangle_str, ';');
            if (semicolon_pos != NULL) {
                *semicolon_pos = '\0';
            }
            double latitude, longtitude;
            if (sscanf(rectangle_str, "%lf,%lf", &longtitude, &latitude) != 2) {
                ESP_LOGE(TAG, "Invalid rectangle in location response: %s", rectangle_str);
                cJSON_Delete(json);
                return ESP_FAIL;
            }
            snprintf(west_south, sizeof(west_south), "%.2f,%.2f", longtitude, latitude);
            update_current_region(raw_region, west_south);
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG, "Error parsing object - [%s] - [%d]", __FILE__, __LINE__);
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t app_location_request(void)
{
    char buf[1024] = {0};
    int ret;
    esp_err_t result = ESP_FAIL;
    bool ssl_handshake_done = false;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_x509_crt cacert;
    mbedtls_ssl_config conf;
    mbedtls_net_context server_fd;

#ifdef CONFIG_MBEDTLS_SSL_PROTO_TLS1_3
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "Failed to initialize PSA crypto, returned %d", (int) status);
        return ESP_FAIL;
    }
#endif

    mbedtls_ssl_init(&ssl);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_net_init(&server_fd);

    ESP_LOGI(TAG, "Seeding the random number generator");
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Attaching the certificate bundle...");
    ret = esp_crt_bundle_attach(&conf);
    if (ret < 0) {
        ESP_LOGE(TAG, "esp_crt_bundle_attach returned -0x%x", -ret);
        goto cleanup;
    }

    ret = mbedtls_ssl_config_defaults(&conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_config_defaults returned %d", ret);
        goto cleanup;
    }

    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
#ifdef CONFIG_MBEDTLS_DEBUG
    mbedtls_esp_enable_debug_log(&conf, CONFIG_MBEDTLS_DEBUG_LEVEL);
#endif

    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_setup returned -0x%x", -ret);
        goto cleanup;
    }

    ret = mbedtls_ssl_set_hostname(&ssl, WEB_SERVER);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ssl_set_hostname returned -0x%x", -ret);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Connecting to %s:%s...", WEB_SERVER, WEB_PORT);
    ret = mbedtls_net_connect(&server_fd, WEB_SERVER, WEB_PORT, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_net_connect returned -%x", -ret);
        goto cleanup;
    }

    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    ESP_LOGI(TAG, "Performing the SSL/TLS handshake...");
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "mbedtls_ssl_handshake returned -0x%x", -ret);
            goto cleanup;
        }
    }
    ssl_handshake_done = true;

    int flags = mbedtls_ssl_get_verify_result(&ssl);
    if (flags != 0) {
        ESP_LOGW(TAG, "Failed to verify peer certificate!");
        bzero(buf, sizeof(buf));
        mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", flags);
        ESP_LOGW(TAG, "verification info: %s", buf);
    }

    ESP_LOGI(TAG, "Writing HTTP request...");
    size_t written_bytes = 0;
    while (written_bytes < strlen(REQUEST)) {
        ret = mbedtls_ssl_write(&ssl,
                                (const unsigned char *)REQUEST + written_bytes,
                                strlen(REQUEST) - written_bytes);
        if (ret >= 0) {
            written_bytes += ret;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ) {
            ESP_LOGE(TAG, "mbedtls_ssl_write returned -0x%x", -ret);
            goto cleanup;
        }
    }

    ESP_LOGI(TAG, "Reading HTTP response...");
    do {
        ret = mbedtls_ssl_read(&ssl, (unsigned char *)buf, sizeof(buf) - 1);

#if CONFIG_MBEDTLS_SSL_PROTO_TLS1_3 && CONFIG_MBEDTLS_CLIENT_SSL_SESSION_TICKETS
        if (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            continue;
        }
#endif

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (ret <= 0) {
            ESP_LOGW(TAG, "Location response read failed or closed, ret=%d", ret);
            goto cleanup;
        }

        buf[ret] = '\0';
        result = parse_location_data(buf);
        break;
    } while (1);

cleanup:
    if (ssl_handshake_done) {
        mbedtls_ssl_close_notify(&ssl);
    }
    mbedtls_net_free(&server_fd);
    mbedtls_ssl_free(&ssl);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_ssl_config_free(&conf);
    mbedtls_entropy_free(&entropy);

    static int request_count;
    ESP_LOGI(TAG, "Completed %d location requests", ++request_count);
    return result;
}

static void network_status_event_handler(void *arg, esp_event_base_t event_base,
                                         int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        s_retry_num++;
        if (s_wifi_event_group) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        s_retry_num = 0;
        if (s_wifi_event_group) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

void app_network_set_connected(bool connected)
{
    wifi_connected = connected;
    s_retry_num = connected ? 0 : EXAMPLE_ESP_MAXIMUM_RETRY;

    if (s_wifi_event_group == NULL) {
        return;
    }

    if (connected) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
}

WiFi_Connect_Status wifi_connected_already(void)
{
    WiFi_Connect_Status status;
    if (true == wifi_connected) {
        status = WIFI_STATUS_CONNECTED_OK;
    } else {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            status = WIFI_STATUS_CONNECTING;
        } else {
            status = WIFI_STATUS_CONNECTED_FAILED;
        }
    }
    return status;
}

esp_err_t app_wifi_get_wifi_ssid(char *ssid, size_t len)
{
    wifi_config_t wifi_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        return ESP_FAIL;
    }
    strncpy(ssid, (const char *)wifi_cfg.sta.ssid, len);
    return ESP_OK;
}

esp_err_t send_network_event(net_event_t event)
{
    net_event_t eventOut = event;
    ESP_RETURN_ON_FALSE(wifi_event_queue, ESP_ERR_INVALID_STATE,
                        TAG, "Network event queue is not ready");

    BaseType_t ret_val = xQueueSend(wifi_event_queue, &eventOut, 0);

    if (NET_EVENT_RECONNECT == event) {
        wifi_connected = false;
    }

    ESP_RETURN_ON_FALSE(pdPASS == ret_val, ESP_ERR_INVALID_STATE,
                        TAG, "The last event has not been processed yet");

    return ESP_OK;
}

// static void event_handler(void *arg, esp_event_base_t event_base,
//                           int32_t event_id, void *event_data)
// {
//     if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
//         send_network_event(NET_EVENT_POWERON_SCAN);
//         ESP_LOGI(TAG, "start connect to the AP");
//     } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
//         if (s_reconnect && ++s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
//             esp_wifi_connect();
//             ESP_LOGI(TAG, "sta disconnect, retry attempt %d...", s_retry_num);
//         } else {
//             ESP_LOGI(TAG, "sta disconnected");
//         }
//         xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
//         xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
//         wifi_connected = false;
//     } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
//         ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
//         ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
//         s_retry_num = 0;
//         wifi_connected = true;
//         xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
//     }
// }

static void wifi_reconnect_sta(void)
{
    int bits = s_wifi_event_group ?
               xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, 0, 1, 0) : 0;

    if (bits & WIFI_CONNECTED_BIT) {
        s_reconnect = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        xEventGroupWaitBits(s_wifi_event_group, WIFI_FAIL_BIT, 0, 1, portTICK_RATE_MS);
    }

    s_reconnect = true;
    s_retry_num = 0;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());

    ESP_LOGI(TAG, "wifi_reconnect_sta finished");

    if (s_wifi_event_group) {
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, 0, 1, pdMS_TO_TICKS(5000));
    }
}

// static void wifi_init_sta(void)
// {
//     s_wifi_event_group = xEventGroupCreate();

//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK(esp_event_loop_create_default());
//     esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
//     assert(sta_netif);

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));

//     esp_event_handler_instance_t instance_any_id;
//     esp_event_handler_instance_t instance_got_ip;
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
//                                                         ESP_EVENT_ANY_ID,
//                                                         &event_handler,
//                                                         NULL,
//                                                         &instance_any_id));
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
//                                                         IP_EVENT_STA_GOT_IP,
//                                                         &event_handler,
//                                                         NULL,
//                                                         &instance_got_ip));
//     wifi_config_t wifi_config = {
//         .sta = {
//             .ssid = "TP-LINK_Liu",
//             .password = "11112222",
//             /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
//              * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
//              * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
//              * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
//              */
//             .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
//             .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
//             .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
//         },
//     };

//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//     ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
//     ESP_ERROR_CHECK(esp_wifi_start());
//     ESP_LOGI(TAG, "wifi_init_sta finished.%s, %s", 
//              wifi_config.sta.ssid, wifi_config.sta.password);

// }

static void network_task(void *args)
{
    static WiFi_Connect_Status wifi_status;
    net_event_t net_event;
    struct tm timeinfo;
    bool weather_sent_today = false;
    TickType_t last_weather_tick = 0;
    // wifi_init_sta();

    while (1) {
        if (pdPASS == xQueueReceive(wifi_event_queue, &net_event, pdMS_TO_TICKS(1000))) {
            ESP_LOGI(TAG, "net_event:%d", net_event);
            switch (net_event) {
            case NET_EVENT_RECONNECT:
                ESP_LOGI(TAG, "NET_EVENT_RECONNECT");
                wifi_reconnect_sta();
                break;
            case NET_EVENT_SCAN:
                ESP_LOGI(TAG, "NET_EVENT_SCAN");
                break;
            case NET_EVENT_NTP:
                ESP_LOGI(TAG, "NET_EVENT_NTP");
                if (wifi_connected) {
                    app_sntp_init();
                } else {
                    ESP_LOGW(TAG, "Skip SNTP request because Wi-Fi is offline");
                }
                break;
            case NET_EVENT_WEATHER:
                ESP_LOGI(TAG, "NET_EVENT_WEATHER");
                if (!wifi_connected) {
                    ESP_LOGW(TAG, "Skip weather request because Wi-Fi is offline");
                    break;
                }
                if (app_location_request() != ESP_OK) {
                    ESP_LOGW(TAG, "Location request failed, use fallback location %s", west_south);
                }
                app_weather_request(west_south);
                last_weather_tick = xTaskGetTickCount();
                break;

            case NET_EVENT_POWERON_SCAN:
                esp_wifi_connect();
                break;
            default:
                ESP_LOGI(TAG, "BREAK");
                break;
            }
        }

        if (wifi_status != wifi_connected_already()) {
            wifi_status = wifi_connected_already();
            ESP_LOGI(TAG, "wifi_connected changed:[%d]", wifi_status);
            if (wifi_connected) {
                //send a weather request at first connected
                send_network_event(NET_EVENT_NTP);
                send_network_event(NET_EVENT_WEATHER);
                weather_sent_today = true;
            }
        } else {
            if (wifi_connected && (last_weather_tick == 0 ||
                    xTaskGetTickCount() - last_weather_tick >= pdMS_TO_TICKS(WEATHER_UPDATE_PERIOD_MS))) {
                send_network_event(NET_EVENT_WEATHER);
            }

            //send a weather request at 12:00am everyday
            time_t now;
            time(&now);
            localtime_r(&now, &timeinfo);

            if (wifi_connected && timeinfo.tm_hour == 12 && timeinfo.tm_min == 0 && !weather_sent_today) {
                send_network_event(NET_EVENT_WEATHER);
                weather_sent_today = true;
                ESP_LOGI(TAG, "Daily weather event sent at 12:00.");
            }
            if (timeinfo.tm_hour != 12 || timeinfo.tm_min > 0) {
                weather_sent_today = false;
            }
        }
    }
    vTaskDelete(NULL);
}

void app_network_start(void)
{
    BaseType_t ret_val;

    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
        ESP_ERROR_CHECK_WITHOUT_ABORT((s_wifi_event_group) ? ESP_OK : ESP_FAIL);
    }

    if (!s_network_handlers_registered) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(WIFI_EVENT,
                                      WIFI_EVENT_STA_DISCONNECTED,
                                      &network_status_event_handler,
                                      NULL));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT,
                                      IP_EVENT_STA_GOT_IP,
                                      &network_status_event_handler,
                                      NULL));
        s_network_handlers_registered = true;
    }

    wifi_event_queue = xQueueCreate(4, sizeof(net_event_t));
    ESP_ERROR_CHECK_WITHOUT_ABORT((wifi_event_queue) ? ESP_OK : ESP_FAIL);

    ret_val = xTaskCreatePinnedToCore(network_task, "NetWork Task", 8 * 1024, NULL, 1, NULL, 0);
    ESP_ERROR_CHECK_WITHOUT_ABORT((pdPASS == ret_val) ? ESP_OK : ESP_FAIL);
}
