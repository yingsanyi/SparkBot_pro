#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEATHER_WIFI_IDLE = 0,
    WEATHER_WIFI_CONNECTING,
    WEATHER_WIFI_CONNECTED,
    WEATHER_WIFI_FAILED,
} weather_wifi_status_t;

typedef struct {
    bool valid;
    bool wifi_connected;
    char location[32];
    char temp_c[8];
    char condition[24];
    char icon[8];
    char obs_time[24];
    char message[80];
    esp_err_t last_error;
} weather_report_t;

esp_err_t weather_service_init(void);
esp_err_t weather_service_connect(void);
esp_err_t weather_service_fetch(weather_report_t *report);
weather_wifi_status_t weather_service_wifi_status(void);
const char *weather_service_configured_ssid(void);
const char *weather_service_configured_location(void);
bool weather_service_has_required_config(void);
void weather_report_set_message(weather_report_t *report, const char *message, esp_err_t err);

#ifdef __cplusplus
}
#endif
