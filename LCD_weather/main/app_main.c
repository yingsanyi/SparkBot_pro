#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "protocol_examples_common.h"

#include "lvgl.h"
#include "iot_button.h"

#include "esp_sparkbot_bsp.h"

#include "ui.h"
#include "app_wifi.h"
#include "app_weather.h"
#include "app_display.h"
#include "app_power.h"

static const char *TAG = "main";
static void button_handler(touch_button_handle_t out_handle, touch_button_message_t *out_message, void *arg)
{
    (void) out_handle; //Unused
    lv_obj_t *current_screen = lv_disp_get_scr_act(NULL);
    int button = (int)arg;

    if (out_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {
        // ESP_LOGI(TAG, "Button[%d] Press", (int)arg);
        for (int i = 0; i < UI_PAGE_COUNT; i++) {
            if (ui_pages[i].page == current_screen) {
                printf("current screen is %s\n", ui_pages[i].name);
                break;
            }
        }
        switch (button) {
        case 1:
            ui_send_sys_event(current_screen, LV_EVENT_PRESSED, NULL);
            break;
        case 2:
            ui_send_sys_event(current_screen, LV_EVENT_SCREEN_PRIVIOUS, NULL);
            break;
        case 3:
            ui_send_sys_event(current_screen, LV_EVENT_SCREEN_NEXT, NULL);
            break;
        default:
            break;
        }
    } else if (out_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) {
        // ESP_LOGI(TAG, "Button[%d] Release", (int)arg);
    } else if (out_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {
        // ESP_LOGI(TAG, "Button[%d] LongPress", (int)arg);
        switch (button) {
        case 1:
            ui_send_sys_event(current_screen, LV_EVENT_LONG_PRESSED, NULL);
            break;
        default:
            break;
        }
    }
}

static void button_long_press_cb(void *arg, void *usr_data)
{
    ESP_LOGI(TAG, "BUTTON_LONG_PRESS_START");
    nvs_flash_erase();
    esp_restart();
}

void memory_monitor()
{
    static char buffer[128];    /* Make sure buffer is enough for `sprintf` */
    if (1) {
        sprintf(buffer, "   Biggest /     Free /    Total\n"
                "\t  SRAM : [%8d / %8d / %8d]\n"
                "\t PSRAM : [%8d / %8d / %8d]",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
        ESP_LOGI("MEM", "%s", buffer);
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

static void network_connect_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Starting WiFi connect task");
    esp_err_t err = example_connect();
    app_network_set_connected(err == ESP_OK);

    if (err == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(send_network_event(NET_EVENT_WEATHER));
    } else {
        ESP_LOGW(TAG, "WiFi connect failed: %s. UI will keep running with cached/default weather.", esp_err_to_name(err));
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Initialize NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    /* Initialize the power adc */
    power_adc_init();
    bsp_i2c_init();

    /**
     * @brief Connect to the network
     */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK_WITHOUT_ABORT(app_weather_start());

    err = app_display_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display/UI init failed: %s", esp_err_to_name(err));
        return;
    }

    bsp_touch_button_create(button_handler);

    /* Create GPIO button */
    button_config_t gpio_btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = 0,
            .active_level = 0,
        },
    };
    button_handle_t gpio_btn = iot_button_create(&gpio_btn_cfg);

    if (NULL == gpio_btn) {
        ESP_LOGE(TAG, "Button create failed");
    }

    if (gpio_btn != NULL) {
        iot_button_register_cb(gpio_btn, BUTTON_LONG_PRESS_START, button_long_press_cb, NULL);
    }

    app_network_start();
    BaseType_t task_ret = xTaskCreatePinnedToCore(network_connect_task, "WiFi Connect Task",
                                                  6 * 1024, NULL, 4, NULL, 0);
    ESP_ERROR_CHECK_WITHOUT_ABORT((task_ret == pdPASS) ? ESP_OK : ESP_FAIL);

    /* Monitor free heap */
    memory_monitor();
}
