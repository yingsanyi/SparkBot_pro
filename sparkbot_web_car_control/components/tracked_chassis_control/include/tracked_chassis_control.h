#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRACKED_CHASSIS_UART_TXD GPIO_NUM_38
#define TRACKED_CHASSIS_UART_RXD GPIO_NUM_48
#define TRACKED_CHASSIS_UART_RTS GPIO_NUM_NC
#define TRACKED_CHASSIS_UART_CTS GPIO_NUM_NC

esp_err_t tracked_chassis_control_start(void);
esp_err_t tracked_chassis_send_motion(float x, float y);
esp_err_t tracked_chassis_rgb_light_control(uint8_t rgb_mode);
esp_err_t tracked_chassis_set_dance_mode(uint8_t dance_mode);
esp_err_t tracked_chassis_set_correction(float correction);

#ifdef __cplusplus
}
#endif
