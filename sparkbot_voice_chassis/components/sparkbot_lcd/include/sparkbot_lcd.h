#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPARKBOT_LCD_STATE_BOOT = 0,
    SPARKBOT_LCD_STATE_IDLE,
    SPARKBOT_LCD_STATE_WAKE,
    SPARKBOT_LCD_STATE_LISTENING,
    SPARKBOT_LCD_STATE_RUNNING,
    SPARKBOT_LCD_STATE_TIMEOUT,
    SPARKBOT_LCD_STATE_ERROR,
} sparkbot_lcd_state_t;

esp_err_t sparkbot_lcd_init(void);
void sparkbot_lcd_show_state(sparkbot_lcd_state_t state, const char *line1, const char *line2);
void sparkbot_lcd_post_state(sparkbot_lcd_state_t state, const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif
