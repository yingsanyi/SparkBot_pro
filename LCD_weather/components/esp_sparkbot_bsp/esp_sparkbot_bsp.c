/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/spi_master.h"
#include "driver/ledc.h"

#include "touch_element/touch_button.h"

#include "bsp/esp_sparkbot_bsp.h"
#include "bsp_err_check.h"
#include "bsp/display.h"

static const char *TAG = "ESP-SparkBot-BSP";
static esp_lcd_panel_handle_t s_lcd_panel_handle = NULL;
static uint16_t *s_lcd_fill_buf = NULL;
static size_t s_lcd_fill_buf_pixels = 0;
static SemaphoreHandle_t s_lcd_flush_done_sem = NULL;
static bool s_lcd_direct_flush_wait = false;


// Touch Button
// #define CONFIG_TOUCH_ELEM_EVENT     1
#define CONFIG_TOUCH_ELEM_CALLBACK  1
#define TOUCH_BUTTON_NUM     3
/* Touch buttons handle */
static touch_button_handle_t button_handle[TOUCH_BUTTON_NUM];
static i2c_bus_handle_t i2c_bus_handle = NULL;

/* Touch buttons channel array */
static const touch_pad_t channel_array[TOUCH_BUTTON_NUM] = {
    TOUCH_PAD_NUM1,
    TOUCH_PAD_NUM2,
    TOUCH_PAD_NUM3,
};

/* Touch buttons channel sensitivity array */
static const float channel_sens_array[TOUCH_BUTTON_NUM] = {
    0.035F,
    0.08F,
    0.08F,
};

static bool i2c_initialized = false;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static adc_oneshot_unit_handle_t bsp_adc_handle = NULL;
#endif

static const button_config_t bsp_button_config[BSP_BUTTON_NUM] = {
    {
        .type = BUTTON_TYPE_ADC,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .adc_button_config.adc_handle = &bsp_adc_handle,
#endif
        .adc_button_config.adc_channel = ADC_CHANNEL_0, // ADC1 channel 0 is GPIO1
        .adc_button_config.button_index = BSP_BUTTON_MENU,
        .adc_button_config.min = 2310, // middle is 2410mV
        .adc_button_config.max = 2510
    },
    {
        .type = BUTTON_TYPE_ADC,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .adc_button_config.adc_handle = &bsp_adc_handle,
#endif
        .adc_button_config.adc_channel = ADC_CHANNEL_0, // ADC1 channel 0 is GPIO1
        .adc_button_config.button_index = BSP_BUTTON_PLAY,
        .adc_button_config.min = 1880, // middle is 1980mV
        .adc_button_config.max = 2080
    },
    {
        .type = BUTTON_TYPE_ADC,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .adc_button_config.adc_handle = &bsp_adc_handle,
#endif
        .adc_button_config.adc_channel = ADC_CHANNEL_0, // ADC1 channel 0 is GPIO1
        .adc_button_config.button_index = BSP_BUTTON_DOWN,
        .adc_button_config.min = 720, // middle is 820mV
        .adc_button_config.max = 920
    },
    {
        .type = BUTTON_TYPE_ADC,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .adc_button_config.adc_handle = &bsp_adc_handle,
#endif
        .adc_button_config.adc_channel = ADC_CHANNEL_0, // ADC1 channel 0 is GPIO1
        .adc_button_config.button_index = BSP_BUTTON_UP,
        .adc_button_config.min = 280, // middle is 380mV
        .adc_button_config.max = 480
    },
    {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config.active_level = 0,
        .gpio_button_config.gpio_num = BSP_BUTTON_BOOT_IO
    }
};

esp_err_t bsp_i2c_init(void)
{
    /* I2C was initialized before */
    if (i2c_initialized) {
        return ESP_OK;
    }

    const i2c_config_t i2c_bus_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BSP_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = BSP_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000
    };
    i2c_bus_handle = i2c_bus_create(BSP_I2C_NUM, &i2c_bus_conf);

    i2c_initialized = true;

    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    i2c_bus_delete(&i2c_bus_handle);
    i2c_initialized = false;
    return ESP_OK;
}

i2c_bus_handle_t bsp_i2c_get_handle(void)
{
    return i2c_bus_handle;
}



#define LCD_CMD_BITS         (8)
#define LCD_PARAM_BITS       (8)
#define LCD_LEDC_CH          (CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH)
#define LVGL_TICK_PERIOD_MS  (CONFIG_BSP_DISPLAY_LVGL_TICK)
#define LVGL_MAX_SLEEP_MS    (CONFIG_BSP_DISPLAY_LVGL_MAX_SLEEP)
#define LCD_TRANSFER_TIMEOUT_MS (1000)

static bool lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel_io;
    (void)edata;

    BaseType_t high_task_wakeup = pdFALSE;
    SemaphoreHandle_t flush_done_sem = (SemaphoreHandle_t)user_ctx;
    if (flush_done_sem) {
        xSemaphoreGiveFromISR(flush_done_sem, &high_task_wakeup);
    }
    return high_task_wakeup == pdTRUE;
}

static esp_err_t lcd_wait_flush_done(void)
{
    if (s_lcd_flush_done_sem == NULL) {
        return ESP_OK;
    }

    if (xSemaphoreTake(s_lcd_flush_done_sem, pdMS_TO_TICKS(LCD_TRANSFER_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Timed out waiting for LCD SPI transfer");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t lcd_ensure_flush_sem(void)
{
    if (s_lcd_flush_done_sem == NULL) {
        s_lcd_flush_done_sem = xSemaphoreCreateBinary();
        ESP_RETURN_ON_FALSE(s_lcd_flush_done_sem, ESP_ERR_NO_MEM, TAG, "LCD flush semaphore alloc failed");
    }
    return ESP_OK;
}

esp_err_t bsp_display_brightness_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BSP_LCD_BACKLIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    return ESP_OK;
}

esp_err_t bsp_display_backlight_off(void)
{
    ESP_LOGI(TAG, "Backlight off GPIO%d level=0", BSP_LCD_BACKLIGHT);
    return gpio_set_level(BSP_LCD_BACKLIGHT, 0);
}

esp_err_t bsp_display_backlight_on(void)
{
    ESP_LOGI(TAG, "Backlight on GPIO%d level=1", BSP_LCD_BACKLIGHT);
    return gpio_set_level(BSP_LCD_BACKLIGHT, 1);
}

esp_err_t bsp_display_backlight_diagnostic(void)
{
    ESP_LOGI(TAG, "Backlight diagnostic start on GPIO%d", BSP_LCD_BACKLIGHT);
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_LCD_BACKLIGHT, 0), TAG, "Backlight GPIO level 0 failed");
    vTaskDelay(pdMS_TO_TICKS(600));
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_LCD_BACKLIGHT, 1), TAG, "Backlight GPIO level 1 failed");
    vTaskDelay(pdMS_TO_TICKS(1200));
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_LCD_BACKLIGHT, 0), TAG, "Backlight GPIO level 0 failed");
    vTaskDelay(pdMS_TO_TICKS(600));
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_LCD_BACKLIGHT, 1), TAG, "Backlight GPIO level 1 failed");
    vTaskDelay(pdMS_TO_TICKS(600));
    ESP_LOGI(TAG, "Backlight diagnostic done");
    return ESP_OK;
}

esp_err_t bsp_display_fill_color(uint16_t color)
{
    if (s_lcd_panel_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_lcd_direct_flush_wait) {
        ESP_RETURN_ON_ERROR(lcd_ensure_flush_sem(), TAG, "LCD flush semaphore init failed");
    }

    const int rows = 10;
    const size_t pixels = BSP_LCD_H_RES * rows;
    if (s_lcd_fill_buf_pixels < pixels) {
        uint16_t *new_buf = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (new_buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        s_lcd_fill_buf = new_buf;
        s_lcd_fill_buf_pixels = pixels;
    }

    for (size_t i = 0; i < pixels; i++) {
        s_lcd_fill_buf[i] = color;
    }

    ESP_LOGI(TAG, "LCD direct fill color=0x%04x", color);
    esp_err_t ret = ESP_OK;
    for (int y = 0; y < BSP_LCD_V_RES; y += rows) {
        int y_end = y + rows;
        if (y_end > BSP_LCD_V_RES) {
            y_end = BSP_LCD_V_RES;
        }
        ret = esp_lcd_panel_draw_bitmap(s_lcd_panel_handle, 0, y, BSP_LCD_H_RES, y_end, s_lcd_fill_buf);
        if (ret != ESP_OK) {
            break;
        }
        if (s_lcd_direct_flush_wait) {
            ret = lcd_wait_flush_done();
            if (ret != ESP_OK) {
                break;
            }
        }
    }

    return ret;
}

static esp_err_t bsp_display_fill_panel_color(esp_lcd_panel_handle_t panel, uint16_t color)
{
    esp_lcd_panel_handle_t saved_panel = s_lcd_panel_handle;
    s_lcd_panel_handle = panel;
    esp_err_t ret = bsp_display_fill_color(color);
    s_lcd_panel_handle = saved_panel;
    return ret;
}

esp_err_t bsp_display_pre_lvgl_diagnostic(void)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    const bsp_display_config_t config = {
        .max_transfer_sz = BSP_LCD_H_RES * 10 * sizeof(uint16_t),
    };

    ESP_LOGI(TAG, "Pre-LVGL LCD diagnostic start");
    s_lcd_direct_flush_wait = true;
    ESP_RETURN_ON_ERROR(bsp_display_new(&config, &panel_handle, &io_handle), TAG, "Pre-LVGL display init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), TAG, "Pre-LVGL display on failed");

    const struct {
        int x_gap;
        int y_gap;
        uint16_t color;
        const char *name;
    } tests[] = {
        {0, 0, 0xffff, "gap 0,0 white"},
        {0, 0, 0xf800, "gap 0,0 red"},
        {0, 0, 0x07e0, "gap 0,0 green"},
        {0, 0, 0x001f, "gap 0,0 blue"},
    };

    for (int level = 0; level <= 1; level++) {
        ESP_LOGI(TAG, "Pre-LVGL backlight level=%d", level);
        gpio_set_level(BSP_LCD_BACKLIGHT, level);
        vTaskDelay(pdMS_TO_TICKS(400));

        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            ESP_LOGI(TAG, "Pre-LVGL fill %s", tests[i].name);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_set_gap(panel_handle, tests[i].x_gap, tests[i].y_gap));
            ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_display_fill_panel_color(panel_handle, tests[i].color));
            vTaskDelay(pdMS_TO_TICKS(650));
        }
    }

    ESP_LOGI(TAG, "Pre-LVGL LCD diagnostic done");
    gpio_set_level(BSP_LCD_BACKLIGHT, 1);
    s_lcd_panel_handle = NULL;
    if (s_lcd_flush_done_sem) {
        vSemaphoreDelete(s_lcd_flush_done_sem);
        s_lcd_flush_done_sem = NULL;
    }
    if (panel_handle) {
        esp_lcd_panel_del(panel_handle);
    }
    if (io_handle) {
        esp_lcd_panel_io_del(io_handle);
    }
    spi_bus_free(BSP_LCD_SPI_NUM);
    s_lcd_direct_flush_wait = false;
    return ESP_OK;
}

esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;
    assert(config != NULL && config->max_transfer_sz > 0);
    if (ret_panel) {
        *ret_panel = NULL;
    }
    if (ret_io) {
        *ret_io = NULL;
    }

    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Brightness init failed");
    if (s_lcd_direct_flush_wait) {
        ESP_RETURN_ON_ERROR(lcd_ensure_flush_sem(), TAG, "LCD flush semaphore init failed");
    }

    ESP_LOGI(TAG, "Initialize LCD SPI bus host=%d mode=%d pclk=%d", BSP_LCD_SPI_NUM, BSP_LCD_SPI_MODE, BSP_LCD_PIXEL_CLOCK_HZ);
    const spi_bus_config_t buscfg = {
        .sclk_io_num = BSP_LCD_SPI_CLK,
        .mosi_io_num = BSP_LCD_SPI_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = config->max_transfer_sz,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");

    ESP_LOGI(TAG, "Install LCD panel IO");
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = BSP_LCD_SPI_CS,
        .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = BSP_LCD_SPI_MODE,
        .trans_queue_depth = 10,
    };
    if (s_lcd_direct_flush_wait) {
        io_config.on_color_trans_done = lcd_color_trans_done_cb;
        io_config.user_ctx = s_lcd_flush_done_sem;
    }
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, ret_io), err, TAG, "New panel IO failed");

    ESP_LOGD(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .color_space = BSP_LCD_COLOR_SPACE,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7789(*ret_io, &panel_config, ret_panel), err, TAG, "New panel failed");


    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(*ret_panel), err, TAG, "Panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(*ret_panel), err, TAG, "Panel init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_set_gap(*ret_panel, BSP_LCD_X_GAP, BSP_LCD_Y_GAP), err, TAG, "Panel set gap failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_invert_color(*ret_panel, true), err, TAG, "Panel invert failed");
    s_lcd_panel_handle = *ret_panel;
    return ret;

err:
    s_lcd_panel_handle = NULL;
    if (*ret_panel) {
        esp_lcd_panel_del(*ret_panel);
    }
    if (*ret_io) {
        esp_lcd_panel_io_del(*ret_io);
    }
    spi_bus_free(BSP_LCD_SPI_NUM);
    return ret;
}

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_display_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg)
{
    assert(cfg != NULL);
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = cfg->trans_size ? (cfg->trans_size * sizeof(uint16_t)): (BSP_LCD_DRAW_BUFF_SIZE * sizeof(uint16_t)),
    };
    BSP_ERROR_CHECK_RETURN_NULL(bsp_display_new(&bsp_disp_cfg, &panel_handle, &io_handle));

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_lcd_panel_disp_off(panel_handle, false);
#else
    esp_lcd_panel_disp_on_off(panel_handle, true);
#endif

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = cfg->buffer_size,
        .trans_size = cfg->trans_size,
        .double_buffer = cfg->double_buffer,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = cfg->flags.buff_dma,
            .buff_spiram = cfg->flags.buff_spiram,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = (BSP_LCD_BIGENDIAN ? true : false),
#endif
        }
    };

    return lvgl_port_add_disp(&disp_cfg);
}

lv_display_t *bsp_display_start(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = {
            .task_priority = CONFIG_BSP_DISPLAY_LVGL_TASK_PRIORITY,
            .task_stack = 6144,
            .task_affinity = 1,
            .timer_period_ms = LVGL_TICK_PERIOD_MS,
            .task_max_sleep_ms = LVGL_MAX_SLEEP_MS,
        },
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        }
    };
    return bsp_display_start_with_config(&cfg);
}

lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg)
{
    lv_display_t *disp;
    assert(cfg != NULL);
    BSP_ERROR_CHECK_RETURN_NULL(lvgl_port_init(&cfg->lvgl_port_cfg));
    BSP_NULL_CHECK(disp = bsp_display_lcd_init(cfg), NULL);

    return disp;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return NULL;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}

void bsp_display_rotate(lv_display_t *disp, lv_disp_rotation_t rotation)
{
    lv_disp_set_rotation(disp, rotation);
}
#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)




esp_err_t bsp_iot_button_create(button_handle_t btn_array[], int *btn_cnt, int btn_array_size)
{
    esp_err_t ret = ESP_OK;
    if ((btn_array_size < BSP_BUTTON_NUM) ||
            (btn_array == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    /* Initialize ADC and get ADC handle */
    BSP_ERROR_CHECK_RETURN_NULL(bsp_adc_initialize());
    bsp_adc_handle = bsp_adc_get_handle();
#endif

    if (btn_cnt) {
        *btn_cnt = 0;
    }
    for (int i = 0; i < BSP_BUTTON_NUM; i++) {
        btn_array[i] = iot_button_create(&bsp_button_config[i]);
        if (btn_array[i] == NULL) {
            ret = ESP_FAIL;
            break;
        }
        if (btn_cnt) {
            (*btn_cnt)++;
        }
    }
    return ret;
}


#ifdef CONFIG_TOUCH_ELEM_EVENT
/* Button event handler task */
static void button_handler_task(void *arg)
{
    (void) arg; //Unused
    touch_elem_message_t element_message;
    while (1) {
        /* Waiting for touch element messages */
        touch_element_message_receive(&element_message, portMAX_DELAY);
        if (element_message.element_type != TOUCH_ELEM_TYPE_BUTTON) {
            continue;
        }
        /* Decode message */
        const touch_button_message_t *button_message = touch_button_get_message(&element_message);
        if (button_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {
            ESP_LOGI(TAG, "Button[%d] Press", (int)element_message.arg);
        } else if (button_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) {
            ESP_LOGI(TAG, "Button[%d] Release", (int)element_message.arg);
        } else if (button_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {
            ESP_LOGI(TAG, "Button[%d] LongPress", (int)element_message.arg);
        }
    }
}
#elif CONFIG_TOUCH_ELEM_CALLBACK
/* Button callback routine */
// static void button_handler(touch_button_handle_t out_handle, touch_button_message_t *out_message, void *arg)
// {
//     (void) out_handle; //Unused
//     if (out_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {
//         ESP_LOGI(TAG, "Button[%d] Press", (int)arg);
//     } else if (out_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) {
//         ESP_LOGI(TAG, "Button[%d] Release", (int)arg);
//     } else if (out_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {
//         ESP_LOGI(TAG, "Button[%d] LongPress", (int)arg);
//     }
// }
#endif

void bsp_touch_button_create(touch_button_callback_t button_callback)
{
    /* Initialize Touch Element library */
    touch_elem_global_config_t global_config = TOUCH_ELEM_GLOBAL_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(touch_element_install(&global_config));
    ESP_LOGI(TAG, "Touch element library installed");

    touch_button_global_config_t button_global_config = TOUCH_BUTTON_GLOBAL_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(touch_button_install(&button_global_config));
    ESP_LOGI(TAG, "Touch button installed");
    for (int i = 0; i < TOUCH_BUTTON_NUM; i++) {
        touch_button_config_t button_config = {
            .channel_num = channel_array[i],
            .channel_sens = channel_sens_array[i]
        };
        /* Create Touch buttons */
        ESP_ERROR_CHECK(touch_button_create(&button_config, &button_handle[i]));
        /* Subscribe touch button events (On Press, On Release, On LongPress) */
        ESP_ERROR_CHECK(touch_button_subscribe_event(button_handle[i],
                                                    TOUCH_ELEM_EVENT_ON_PRESS | TOUCH_ELEM_EVENT_ON_RELEASE | TOUCH_ELEM_EVENT_ON_LONGPRESS,
                                                     (void *)channel_array[i]));
#ifdef CONFIG_TOUCH_ELEM_EVENT
        /* Set EVENT as the dispatch method */
        ESP_ERROR_CHECK(touch_button_set_dispatch_method(button_handle[i], TOUCH_ELEM_DISP_EVENT));
#elif CONFIG_TOUCH_ELEM_CALLBACK
        /* Set EVENT as the dispatch method */
        ESP_ERROR_CHECK(touch_button_set_dispatch_method(button_handle[i], TOUCH_ELEM_DISP_CALLBACK));
        /* Register a handler function to handle event messages */
        ESP_ERROR_CHECK(touch_button_set_callback(button_handle[i], button_callback));
#endif
        /* Set LongPress event trigger threshold time */
        ESP_ERROR_CHECK(touch_button_set_longpress(button_handle[i], 2000));
    }
    ESP_LOGI(TAG, "Touch buttons created");

#ifdef CONFIG_TOUCH_ELEM_EVENT
    /* Create a handler task to handle event messages */
    xTaskCreate(&button_handler_task, "button_handler_task", 4 * 1024, NULL, 5, NULL);
#endif

    touch_element_start();
    ESP_LOGI(TAG, "Touch element library start");
}
