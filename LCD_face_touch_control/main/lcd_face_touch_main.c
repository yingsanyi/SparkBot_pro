/*
 * ESP-SparkBot touch-controlled LCD face demo.
 *
 * Touch key 2 switches to the previous face, touch key 3 switches to the next
 * face, and touch key 1 returns to the idle face.
 */

#include <stdint.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lcd_face_ui.h"
#include "sdkconfig.h"
#include "touch_element/touch_button.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define TOUCH_BUTTON_NUM 3

#ifndef CONFIG_LCD_FACE_TOUCH_LONGPRESS_MS
#define CONFIG_LCD_FACE_TOUCH_LONGPRESS_MS 2000
#endif

typedef enum {
    FACE_ACTION_IDLE = 0,
    FACE_ACTION_PREVIOUS,
    FACE_ACTION_NEXT,
} face_action_t;

typedef struct {
    lcd_face_scene_t scene;
    const char *line1;
    const char *line2;
} face_entry_t;

static const char *TAG = "lcd_face_touch";

static const touch_pad_t s_touch_channels[TOUCH_BUTTON_NUM] = {
    TOUCH_PAD_NUM1,
    TOUCH_PAD_NUM2,
    TOUCH_PAD_NUM3,
};

static const float s_touch_sensitivity[TOUCH_BUTTON_NUM] = {
    0.035F,
    0.08F,
    0.08F,
};

static const face_entry_t s_face_cycle[] = {
    {LCD_FACE_SCENE_IDLE, "IDLE", "READY"},
    {LCD_FACE_SCENE_WINK, "WINK", "TOUCH"},
    {LCD_FACE_SCENE_HAPPY, "HAPPY", "SMILE"},
    {LCD_FACE_SCENE_SURPRISED, "WOW", "TOUCH"},
    {LCD_FACE_SCENE_SLEEPY, "SLEEPY", "REST"},
    {LCD_FACE_SCENE_ANGRY, "ANGRY", "FOCUS"},
};

static touch_button_handle_t s_touch_buttons[TOUCH_BUTTON_NUM];
static QueueHandle_t s_face_action_queue;
static size_t s_face_index;

static void show_face_entry(const face_entry_t *entry)
{
    if (!entry) {
        return;
    }
    lcd_face_ui_show_scene(entry->scene, entry->line1, entry->line2);
}

static void show_current_face(void)
{
    show_face_entry(&s_face_cycle[s_face_index]);
}

static void set_idle_face(void)
{
    s_face_index = 0;
    show_current_face();
}

static void show_previous_face(void)
{
    if (s_face_index == 0) {
        s_face_index = ARRAY_SIZE(s_face_cycle) - 1;
    } else {
        s_face_index--;
    }
    show_current_face();
}

static void show_next_face(void)
{
    s_face_index = (s_face_index + 1) % ARRAY_SIZE(s_face_cycle);
    show_current_face();
}

static void handle_face_action(face_action_t action)
{
    switch (action) {
    case FACE_ACTION_IDLE:
        ESP_LOGI(TAG, "Touch 1: idle face");
        set_idle_face();
        break;
    case FACE_ACTION_PREVIOUS:
        ESP_LOGI(TAG, "Touch 2: previous face");
        show_previous_face();
        break;
    case FACE_ACTION_NEXT:
        ESP_LOGI(TAG, "Touch 3: next face");
        show_next_face();
        break;
    default:
        break;
    }
}

static void face_action_task(void *arg)
{
    (void)arg;

    face_action_t action;
    while (true) {
        if (xQueueReceive(s_face_action_queue, &action, portMAX_DELAY) == pdTRUE) {
            handle_face_action(action);
        }
    }
}

static void touch_button_handler(touch_button_handle_t out_handle,
                                 touch_button_message_t *out_message,
                                 void *arg)
{
    (void)out_handle;

    if (!out_message || out_message->event != TOUCH_BUTTON_EVT_ON_PRESS) {
        return;
    }

    const touch_pad_t channel = (touch_pad_t)(intptr_t)arg;
    face_action_t action;

    switch (channel) {
    case TOUCH_PAD_NUM1:
        action = FACE_ACTION_IDLE;
        break;
    case TOUCH_PAD_NUM2:
        action = FACE_ACTION_PREVIOUS;
        break;
    case TOUCH_PAD_NUM3:
        action = FACE_ACTION_NEXT;
        break;
    default:
        return;
    }

    if (s_face_action_queue) {
        xQueueSend(s_face_action_queue, &action, 0);
    }
}

static esp_err_t app_touch_buttons_start(void)
{
    s_face_action_queue = xQueueCreate(4, sizeof(face_action_t));
    ESP_RETURN_ON_FALSE(s_face_action_queue, ESP_ERR_NO_MEM, TAG, "create touch queue failed");

    BaseType_t task_ret = xTaskCreate(face_action_task, "face_action", 4 * 1024, NULL, 4, NULL);
    ESP_RETURN_ON_FALSE(task_ret == pdPASS, ESP_FAIL, TAG, "create face action task failed");

    touch_elem_global_config_t global_config = TOUCH_ELEM_GLOBAL_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(touch_element_install(&global_config), TAG, "install touch element failed");
    ESP_LOGI(TAG, "Touch element library installed");

    touch_button_global_config_t button_global_config = TOUCH_BUTTON_GLOBAL_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(touch_button_install(&button_global_config), TAG, "install touch button failed");
    ESP_LOGI(TAG, "Touch button driver installed");

    for (int i = 0; i < TOUCH_BUTTON_NUM; i++) {
        touch_button_config_t button_config = {
            .channel_num = s_touch_channels[i],
            .channel_sens = s_touch_sensitivity[i],
        };

        ESP_RETURN_ON_ERROR(touch_button_create(&button_config, &s_touch_buttons[i]),
                            TAG, "create touch button failed");
        ESP_RETURN_ON_ERROR(touch_button_subscribe_event(s_touch_buttons[i],
                                                         TOUCH_ELEM_EVENT_ON_PRESS |
                                                             TOUCH_ELEM_EVENT_ON_RELEASE |
                                                             TOUCH_ELEM_EVENT_ON_LONGPRESS,
                                                         (void *)(intptr_t)s_touch_channels[i]),
                            TAG, "subscribe touch button event failed");
        ESP_RETURN_ON_ERROR(touch_button_set_dispatch_method(s_touch_buttons[i], TOUCH_ELEM_DISP_CALLBACK),
                            TAG, "set touch dispatch method failed");
        ESP_RETURN_ON_ERROR(touch_button_set_callback(s_touch_buttons[i], touch_button_handler),
                            TAG, "set touch callback failed");
        ESP_RETURN_ON_ERROR(touch_button_set_longpress(s_touch_buttons[i], CONFIG_LCD_FACE_TOUCH_LONGPRESS_MS),
                            TAG, "set touch longpress failed");
    }

    ESP_RETURN_ON_ERROR(touch_element_start(), TAG, "start touch element failed");
    ESP_LOGI(TAG, "Touch buttons ready: 1 idle, 2 previous, 3 next");
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-SparkBot touch LCD face control starting");

    ESP_ERROR_CHECK(lcd_face_ui_init());
    lcd_face_ui_show_scene(LCD_FACE_SCENE_BOOT, "BOOT", "TOUCH");

    ESP_ERROR_CHECK(app_touch_buttons_start());
    set_idle_face();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
