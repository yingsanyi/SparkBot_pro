/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lv_decoder.h"
#include "bsp/esp-bsp.h"

#include "app_display.h"
#include "app_weather.h"
#include "app_wifi.h"
#include "app_power.h"
#include "ui/ui.h"
#include "mmap_generate_weather.h"

static const char *TAG = "app_display";

mmap_assets_handle_t asset_weather;

esp_lv_decoder_handle_t decoder_handle = NULL;

static esp_err_t app_mount_mmap_fs(void)
{
    const mmap_assets_config_t config_weather = {
        .partition_label = "weather",
        .max_files = MMAP_WEATHER_FILES,
        .checksum = MMAP_WEATHER_CHECKSUM,
        .flags = {
            .mmap_enable = true,
            .app_bin_check = true,
        },
    };

    esp_err_t ret = mmap_assets_new(&config_weather, &asset_weather);
    if (ret != ESP_OK || asset_weather == NULL) {
        ESP_LOGE(TAG, "Failed to mount %s assets: %s", config_weather.partition_label, esp_err_to_name(ret));
        return ret == ESP_OK ? ESP_FAIL : ret;
    }
    ESP_LOGI(TAG, "[%s]stored_files:%d", config_weather.partition_label, mmap_assets_get_stored_files(asset_weather));

    return ESP_OK;
}

static int find_icon_index(const char *data)
{
    const int stored_files = asset_weather ? mmap_assets_get_stored_files(asset_weather) : 0;
    int fallback_index = -1;
    char icon_name[16];

    if (stored_files <= 0) {
        return -1;
    }

    snprintf(icon_name, sizeof(icon_name), "%s.qoi", (data && data[0]) ? data : "104");
    for (int i = 0; i < stored_files; i++) {
        const char *name = mmap_assets_get_name(asset_weather, i);
        if (name == NULL) {
            continue;
        }
        if (!strcmp(name, icon_name)) {
            return i;
        }
        if (!strcmp(name, "999.qoi")) {
            fallback_index = i;
        }
    }

    return fallback_index >= 0 ? fallback_index : 0;
}

static void display_update_weather_icon(void)
{
    static lv_img_dsc_t img_weather_dsc;
    int icon_index = find_icon_index(weather_icon);

    if (ui_weathershow == NULL || icon_index < 0) {
        return;
    }

    const void *img_data = mmap_assets_get_mem(asset_weather, icon_index);
    size_t img_size = mmap_assets_get_size(asset_weather, icon_index);
    if (img_data == NULL || img_size == 0) {
        return;
    }

    img_weather_dsc.data_size = img_size;
    img_weather_dsc.data = img_data;
    lv_img_set_src(ui_weathershow, &img_weather_dsc);
}

struct timeval tv_now = {
    .tv_sec = 0,
    .tv_usec = 0
};

static void display_clock_update(lv_timer_t *timer)
{
    struct tm timeinfo;
    static char hour_str[3], min_str[3], time_str[6];
    static char date_str[16], weekday_str[16];
    static char power_state_text[5];

    gettimeofday(&tv_now, NULL);
    localtime_r(&tv_now.tv_sec, &timeinfo);

    // Format hour, minute and second.
    snprintf(hour_str, sizeof(hour_str), "%02d", timeinfo.tm_hour);
    snprintf(min_str, sizeof(min_str), "%02d", timeinfo.tm_min);
    snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    snprintf(power_state_text, sizeof(power_state_text), "%d%%", get_power_value());

    // Format month and day, for example 10/18.
    snprintf(date_str, sizeof(date_str), "%02d/%02d",
             timeinfo.tm_mon + 1, timeinfo.tm_mday);

    // Get weekday.
    const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    snprintf(weekday_str, sizeof(weekday_str), "%s", weekdays[timeinfo.tm_wday]);

    // Update text of each label.
    if (ui_hour) {
        lv_label_set_text(ui_hour, hour_str);
    }
    if (ui_min) {
        lv_label_set_text(ui_min, min_str);
    }
    if (ui_date) {
        lv_label_set_text(ui_date, date_str);
    }
    if (ui_weekday) {
        lv_label_set_text(ui_weekday, weekday_str);
    }
    if (title_timestate) {
        lv_label_set_text(title_timestate, time_str);
    }

    if (ui_weather) {
        lv_label_set_text(ui_weather, weather_text);
    }
    if (ui_location) {
        lv_label_set_text(ui_location, current_region);
    }
    if (ui_temp) {
        lv_label_set_text(ui_temp, weather_temp);
    }
    if (title_batterytxt) {
        lv_label_set_text(title_batterytxt, power_state_text);
    }
    if (title_powerstate) {
        lv_slider_set_value(title_powerstate, get_power_value(), LV_ANIM_OFF);
    }
    if (title_wifistate) {
        if (wifi_connected_already() == WIFI_STATUS_CONNECTED_OK) {
            lv_img_set_src(title_wifistate, &ui_img_wifi_png);
        } else {
            lv_img_set_src(title_wifistate, &ui_img_wifi_disconnection_png);
        }
    }
    display_update_weather_icon();
}

static void display_init_timer(void)
{
    lv_timer_t *timer_clock = lv_timer_create(display_clock_update, 1000,  NULL);
    if (timer_clock) {
        display_clock_update(timer_clock);
    }
}

static esp_err_t app_lvgl_display(void)
{
    if (!bsp_display_lock(2000)) {
        ESP_LOGE(TAG, "Failed to lock LVGL for UI init");
        return ESP_ERR_TIMEOUT;
    }

    ui_init();
    display_init_timer();

    bsp_display_unlock();
    return ESP_OK;
}

static void app_show_status_screen(const char *text, lv_color_t bg_color)
{
    if (!bsp_display_lock(2000)) {
        ESP_LOGE(TAG, "Failed to lock LVGL for status screen: %s", text);
        return;
    }

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, bg_color, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_center(label);
    lv_disp_load_scr(screen);

    bsp_display_unlock();
}

esp_err_t app_display_start(void)
{
    esp_err_t ret;

    /* Initialize display and LVGL */
    bsp_display_cfg_t custom_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .trans_size = BSP_LCD_H_RES * 10, // in SRAM, DMA-capable
        .double_buffer = 0,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
        }
    };
    custom_cfg.lvgl_port_cfg.task_stack = 1024 * 30;
    custom_cfg.lvgl_port_cfg.task_affinity = 1;
    lv_display_t *disp = bsp_display_start_with_config(&custom_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "LCD/LVGL init failed");
        return ESP_FAIL;
    }

    ret = bsp_display_backlight_on();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Backlight on failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = app_mount_mmap_fs();
    if (ret != ESP_OK) {
        app_show_status_screen("ASSET ERR", lv_color_hex(0xa00000));
        return ret;
    }

    ret = esp_lv_decoder_init(&decoder_handle);
    if (ret != ESP_OK) {
        app_show_status_screen("DECODER ERR", lv_color_hex(0xa00000));
        return ret;
    }

    /* Add and show objects on display */
    ret = app_lvgl_display();
    if (ret != ESP_OK) {
        app_show_status_screen("UI ERR", lv_color_hex(0xa00000));
        return ret;
    }

    return ESP_OK;
}
