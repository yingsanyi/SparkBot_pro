#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_face_ui.h"
#include "sdkconfig.h"

static const char *TAG = "lcd_face_cycle";

#ifndef CONFIG_LCD_FACE_CYCLE_PERIOD_MS
#define CONFIG_LCD_FACE_CYCLE_PERIOD_MS 1500
#endif

typedef struct {
    lcd_face_scene_t scene;
    const char *line1;
    const char *line2;
} scene_entry_t;

static const scene_entry_t s_scene_cycle[] = {
    {LCD_FACE_SCENE_BOOT, "BOOT", "LCD FACE CYCLE"},
    {LCD_FACE_SCENE_IDLE, "IDLE", "WAITING"},
    {LCD_FACE_SCENE_WINK, "WINK", "PLAY MODE"},
    {LCD_FACE_SCENE_HAPPY, "HAPPY", "SMILE"},
    {LCD_FACE_SCENE_SURPRISED, "WOW", "SURPRISED"},
    {LCD_FACE_SCENE_SLEEPY, "SLEEPY", "REST TIME"},
    {LCD_FACE_SCENE_ANGRY, "ANGRY", "FOCUS UP"},
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

void app_main(void)
{
    ESP_LOGI(TAG, "LCD face cycle starting");
    ESP_ERROR_CHECK(lcd_face_ui_init());

    size_t index = 0;
    while (true) {
        const scene_entry_t *entry = &s_scene_cycle[index];
        ESP_LOGI(TAG, "Scene: %s", entry->line1);
        lcd_face_ui_show_scene(entry->scene, entry->line1, entry->line2);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LCD_FACE_CYCLE_PERIOD_MS));
        index = (index + 1) % ARRAY_SIZE(s_scene_cycle);
    }
}
