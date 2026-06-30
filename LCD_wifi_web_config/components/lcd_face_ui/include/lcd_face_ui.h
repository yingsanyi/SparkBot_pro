#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LCD_FACE_SCENE_BOOT = 0,
    LCD_FACE_SCENE_IDLE,
    LCD_FACE_SCENE_WINK,
    LCD_FACE_SCENE_HAPPY,
    LCD_FACE_SCENE_SURPRISED,
    LCD_FACE_SCENE_SLEEPY,
    LCD_FACE_SCENE_ANGRY,
} lcd_face_scene_t;

esp_err_t lcd_face_ui_init(void);
void lcd_face_ui_show_scene(lcd_face_scene_t scene, const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif
