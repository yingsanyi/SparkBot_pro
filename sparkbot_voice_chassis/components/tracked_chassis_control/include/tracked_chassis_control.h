#pragma once

#include <stdint.h>

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRACKED_CHASSIS_UART_TXD (GPIO_NUM_38)
#define TRACKED_CHASSIS_UART_RXD (GPIO_NUM_48)
#define TRACKED_CHASSIS_UART_RTS (UART_PIN_NO_CHANGE)
#define TRACKED_CHASSIS_UART_CTS (UART_PIN_NO_CHANGE)

void tracked_chassis_control_start(void);
void tracked_chassis_motion_control(const char *command);
void tracked_chassis_stop(void);
void tracked_chassis_rgb_light_control(uint8_t rgb_mode);
void tracked_chassis_set_dance_mode(uint8_t dance_mode);

#ifdef __cplusplus
}
#endif
