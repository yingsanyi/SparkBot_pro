/*
 * ESP-SparkBot LCD 背光闪烁测试。
 *
 * 程序启动后先初始化头部 LCD，并把整块屏幕填充为纯色；
 * 随后通过 GPIO46 周期性打开/关闭背光，实现 LCD Blink 效果。
 */

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_types.h"

static const char *TAG = "lcd_blink";

/* 本实验依赖 ESP-SparkBot 头部硬件上的 ESP32-S3 和固定 LCD 引脚。 */
#if !CONFIG_IDF_TARGET_ESP32S3
#error "This LCD test is for the ESP-SparkBot ESP32-S3 head hardware."
#endif

/* LCD 硬件连接：这些 GPIO 来自 ESP-SparkBot BSP 中的 LCD 引脚定义。 */
#define LCD_MOSI_GPIO             47
#define LCD_CLK_GPIO              21
#define LCD_CS_GPIO               44
#define LCD_DC_GPIO               43
#define LCD_RST_GPIO              (-1)
#define LCD_BL_GPIO               46

/* SPI 与 LCD 控制参数：保持与 SparkBot 官方源码中稳定可用的直连屏幕配置一致。 */
#define LCD_HOST                  SPI2_HOST
#define LCD_SPI_MODE              0
#define LCD_PIXEL_CLOCK_HZ        (40 * 1000 * 1000)

/* 屏幕分辨率、命令位宽、参数位宽，以及每次刷新的行数。 */
#define LCD_H_RES                 240
#define LCD_V_RES                 240
#define LCD_CMD_BITS              8
#define LCD_PARAM_BITS            8
#define LCD_FLUSH_LINES           10
#define LCD_TRANSFER_TIMEOUT_MS   1000

#ifndef CONFIG_LCD_BLINK_PERIOD_MS
#define CONFIG_LCD_BLINK_PERIOD_MS 1000
#endif

#define LCD_RGB565_WHITE          0xFFFF

/* LCD 面板句柄在初始化后保存，后续画图函数都通过它访问屏幕。 */
static esp_lcd_panel_handle_t s_panel = NULL;

/*
 * ESP LCD 的 SPI 刷屏是异步传输。
 * 用信号量等待传输完成，避免上一块像素数据还没发完就改写缓冲区。
 */
static SemaphoreHandle_t s_flush_done_sem = NULL;

/* DMA 行缓冲区：一次只准备若干行像素，分块刷完整个 240x240 屏幕。 */
static DMA_ATTR uint16_t s_line_buffer[LCD_H_RES * LCD_FLUSH_LINES];

/* SPI 颜色数据传输完成回调，在中断上下文中释放信号量通知主任务。 */
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

static void lcd_wait_flush_done(void)
{
    /* 等待本次 draw_bitmap 对应的 SPI DMA 传输完成。 */
    if (xSemaphoreTake(s_flush_done_sem, pdMS_TO_TICKS(LCD_TRANSFER_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Timed out waiting for LCD SPI transfer");
        ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
    }
}

/* 把整块 LCD 填充为同一种 RGB565 颜色。 */
static void lcd_fill_color(uint16_t color)
{
    /* 先把一块“行缓冲区”填满指定颜色。 */
    for (int i = 0; i < LCD_H_RES * LCD_FLUSH_LINES; i++) {
        s_line_buffer[i] = color;
    }

    /* LCD 较大，按 LCD_FLUSH_LINES 行为一组分块发送，减少 RAM 占用。 */
    for (int y = 0; y < LCD_V_RES; y += LCD_FLUSH_LINES) {
        const int lines = (LCD_V_RES - y) < LCD_FLUSH_LINES ? (LCD_V_RES - y) : LCD_FLUSH_LINES;
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + lines, s_line_buffer));
        lcd_wait_flush_done();
    }
}

/* 控制 LCD 背光：GPIO46 输出高电平点亮，低电平熄灭。 */
static void lcd_backlight_set(bool on)
{
    ESP_ERROR_CHECK(gpio_set_level(LCD_BL_GPIO, on ? 1 : 0));
}

/* 初始化背光 GPIO，并默认打开背光，方便观察 LCD 初始化结果。 */
static void lcd_backlight_init(void)
{
    const gpio_config_t backlight_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_BL_GPIO,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight_config));
    lcd_backlight_set(true);
}

/* 完成 LCD 工作前所需的全部初始化。 */
static void lcd_init(void)
{
    /* 创建信号量，用于等待每一次 SPI 刷屏传输完成。 */
    s_flush_done_sem = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(s_flush_done_sem ? ESP_OK : ESP_ERR_NO_MEM);

    lcd_backlight_init();

    ESP_LOGI(TAG, "Initialize LCD SPI bus");
    /* 配置 SPI 总线：本屏幕只需要 MOSI 和 SCLK，不使用 MISO。 */
    const spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = LCD_CLK_GPIO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_FLUSH_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install LCD panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    /*
     * 创建 LCD 的 SPI IO 层。
     * CS 负责片选，DC 用来区分当前发送的是命令还是像素数据。
     */
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_CS_GPIO,
        .dc_gpio_num = LCD_DC_GPIO,
        .spi_mode = LCD_SPI_MODE,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = lcd_color_trans_done_cb,
        .user_ctx = s_flush_done_sem,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install ST7789 LCD panel driver");
    /* 创建 ST7789 面板驱动：本屏幕使用 RGB565，即每个像素 16 bit。 */
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));

    ESP_LOGI(TAG, "Reset and initialize LCD");
    /* 按 ESP LCD 驱动流程复位、初始化、设置颜色反相并打开显示。 */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    ESP_LOGI(TAG, "Fill LCD white");
    /* 初始化完成后先填充白屏，后续只闪烁背光，不再重复改屏幕内容。 */
    lcd_fill_color(LCD_RGB565_WHITE);
}

/* ESP-IDF 应用入口，相当于普通 C 程序中的 main()。 */
void app_main(void)
{
    ESP_LOGI(TAG, "LCD backlight blink started");
    lcd_init();

    /* 主循环：按 menuconfig 中的周期反复打开/关闭 LCD 背光。 */
    while (1) {
        ESP_LOGI(TAG, "LCD backlight on");
        lcd_backlight_set(true);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LCD_BLINK_PERIOD_MS));

        ESP_LOGI(TAG, "LCD backlight off");
        lcd_backlight_set(false);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LCD_BLINK_PERIOD_MS));
    }
}
