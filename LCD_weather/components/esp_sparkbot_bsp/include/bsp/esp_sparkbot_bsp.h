/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP BSP: ESP-SparkBot
 */

#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "iot_button.h"
#include "touch_element/touch_button.h"
#include "bsp/config.h"
#include "bsp/display.h"
#include "i2c_bus.h"

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
#include "lvgl.h"
#include "esp_lvgl_port.h"
#endif // BSP_CONFIG_NO_GRAPHIC_LIB == 0

/**************************************************************************************************
 *  BSP Capabilities
 **************************************************************************************************/

#define BSP_CAPS_DISPLAY        1
#define BSP_CAPS_TOUCH          0
#define BSP_CAPS_BUTTONS        1
#define BSP_CAPS_AUDIO          0
#define BSP_CAPS_AUDIO_SPEAKER  0
#define BSP_CAPS_AUDIO_MIC      0
#define BSP_CAPS_SDCARD         0
#define BSP_CAPS_IMU            0
#define BSP_CAPS_CAMERA         0

/**************************************************************************************************
 * ESP-SparkBot-BSP pinout
 **************************************************************************************************/

/* I2C */
#define BSP_I2C_SCL           (GPIO_NUM_5)
#define BSP_I2C_SDA           (GPIO_NUM_4)

/* Display */
#define BSP_LCD_SPI_MOSI      (GPIO_NUM_47)
#define BSP_LCD_SPI_CLK       (GPIO_NUM_21)
#define BSP_LCD_SPI_CS        (GPIO_NUM_44)
#define BSP_LCD_DC            (GPIO_NUM_43)
#define BSP_LCD_RST           (GPIO_NUM_NC)
#define BSP_LCD_BACKLIGHT     (GPIO_NUM_46)


/* Buttons */
#define BSP_BUTTON_BOOT_IO   (GPIO_NUM_0)
#define BSP_BUTTONS_IO       (GPIO_NUM_1) // All 4 buttons mapped to this GPIO
typedef enum bsp_led_t {
    BSP_LED_GREEN = GPIO_NUM_3,
} bsp_led_t;

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
 *
 * Buttons interface
 *
 * Example configuration:
 * \code{.c}
 * button_handle_t btns[BSP_BUTTON_NUM];
 * bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
 * iot_button_register_cb(btns[0], ...
 * \endcode
 **************************************************************************************************/
typedef enum {
    BSP_BUTTON_MENU = 0,
    BSP_BUTTON_PLAY,
    BSP_BUTTON_DOWN,
    BSP_BUTTON_UP,
    BSP_BUTTON_BOOT,
    BSP_BUTTON_NUM
} bsp_button_t;

/**
 * @brief Initialize all buttons
 *
 * Returned button handlers must be used with espressif/button component API
 *
 * @note For LCD panel button which is defined as BSP_BUTTON_MAIN, bsp_display_start should
 *       be called before call this function.
 *
 * @param[out] btn_array      Output button array
 * @param[out] btn_cnt        Number of button handlers saved to btn_array, can be NULL
 * @param[in]  btn_array_size Size of output button array. Must be at least BSP_BUTTON_NUM
 * @return
 *     - ESP_OK               All buttons initialized
 *     - ESP_ERR_INVALID_ARG  btn_array is too small or NULL
 *     - ESP_FAIL             Underlaying iot_button_create failed
 */
esp_err_t bsp_iot_button_create(button_handle_t btn_array[], int *btn_cnt, int btn_array_size);


void bsp_touch_button_create(touch_button_callback_t button_callback);

/**************************************************************************************************
 *
 * I2C interface
 *
 * The weather application uses I2C for board peripherals shared by the LCD.
 **************************************************************************************************/
#define BSP_I2C_NUM     CONFIG_BSP_I2C_NUM

/**
 * @brief Init I2C driver
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *      - ESP_FAIL              I2C driver installation error
 *
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Deinit I2C driver and free its resources
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *
 */
esp_err_t bsp_i2c_deinit(void);

i2c_bus_handle_t bsp_i2c_get_handle(void);

/**************************************************************************************************
 *
 * LCD interface
 *
 * ESP-SparkBot-BSP is shipped with 1.3inch ST7789 display controller.
 * It features 16-bit colors and 240x240 resolution.
 *
 * LVGL is used as graphics library. LVGL is NOT thread safe, therefore the user must take LVGL mutex
 * by calling bsp_display_lock() before calling any LVGL API (lv_...) and then give the mutex with
 * bsp_display_unlock().
 *
 * If you want to use the display without LVGL, see bsp/display.h API and use BSP version with 'noglib' suffix.
 **************************************************************************************************/
#define BSP_LCD_PIXEL_CLOCK_HZ     (40 * 1000 * 1000)
#define BSP_LCD_SPI_NUM            (SPI2_HOST)
#define BSP_LCD_SPI_MODE           (0)
#define BSP_LCD_X_GAP              (0)
#define BSP_LCD_Y_GAP              (0)

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
#define BSP_LCD_DRAW_BUFF_SIZE     (BSP_LCD_H_RES * BSP_LCD_V_RES)
#define BSP_LCD_DRAW_BUFF_DOUBLE   (0)

/**
 * @brief BSP display configuration structure
 */
typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;  /*!< LVGL port configuration */
    uint32_t        buffer_size;    /*!< Size of the buffer for the screen in pixels */
    uint32_t        trans_size;
    bool            double_buffer;  /*!< True, if should be allocated two buffers */
    struct {
        unsigned int buff_dma: 1;    /*!< Allocated LVGL buffer will be DMA capable */
        unsigned int buff_spiram: 1; /*!< Allocated LVGL buffer will be in PSRAM */
    } flags;
} bsp_display_cfg_t;

/**
 * @brief Initialize display
 *
 * This function initializes SPI, display controller and starts LVGL handling task.
 *
 * @return Pointer to LVGL display or NULL when error occurred
 */
lv_display_t *bsp_display_start(void);

/**
 * @brief Initialize display
 *
 * This function initializes SPI, display controller and starts LVGL handling task.
 * LCD backlight must be enabled separately by calling bsp_display_brightness_set()
 *
 * @param cfg display configuration
 *
 * @return Pointer to LVGL display or NULL when error occurred
 */
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg);

/**
 * @brief Fill the display with a solid RGB565 color through the LCD panel driver.
 *
 * This bypasses LVGL and is useful for confirming the SPI panel path is alive.
 *
 * @param color RGB565 color value
 * @return ESP_OK on success
 */
esp_err_t bsp_display_fill_color(uint16_t color);

/**
 * @brief Blink the LCD backlight once for display bring-up diagnostics.
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_display_backlight_diagnostic(void);

/**
 * @brief Run a standalone LCD color test before LVGL is initialized.
 *
 * This creates a temporary ST7789 panel, sends visible test colors with
 * different gap/backlight combinations, then releases the SPI bus so LVGL can
 * initialize the display normally.
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_display_pre_lvgl_diagnostic(void);

/**
 * @brief Get pointer to input device (touch, buttons, ...)
 *
 * @note The LVGL input device is initialized in bsp_display_start() function.
 *
 * @return Pointer to LVGL input device or NULL when not initialized
 */
lv_indev_t *bsp_display_get_input_dev(void);

/**
 * @brief Take LVGL mutex
 *
 * @param timeout_ms Timeout in [ms]. 0 will block indefinitely.
 * @return true  Mutex was taken
 * @return false Mutex was NOT taken
 */
bool bsp_display_lock(uint32_t timeout_ms);

/**
 * @brief Give LVGL mutex
 *
 */
void bsp_display_unlock(void);

/**
 * @brief Rotate screen
 *
 * Display must be already initialized by calling bsp_display_start()
 *
 * @param[in] disp Pointer to LVGL display
 * @param[in] rotation Angle of the display rotation
 */
void bsp_display_rotate(lv_display_t *disp, lv_disp_rotation_t rotation);
#endif // BSP_CONFIG_NO_GRAPHIC_LIB == 0



/**************************************************************************************************
 *
 * ADC interface
 *
 * There are multiple devices connected to ADC peripheral:
 *  - Buttons
 *
 * After initialization of ADC, use adc_handle when using ADC driver.
 **************************************************************************************************/

#define BSP_ADC_UNIT     ADC_UNIT_1

/**
 * @brief Initialize ADC
 *
 * The ADC can be initialized inside BSP, when needed.
 *
 * @param[out] adc_handle Returned ADC handle
 */
esp_err_t bsp_adc_initialize(void);


#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
/**
 * @brief Get ADC handle
 *
 * @note This function is available only in IDF5 and higher
 *
 * @return ADC handle
 */
adc_oneshot_unit_handle_t bsp_adc_get_handle(void);
#endif

#ifdef __cplusplus
}
#endif
