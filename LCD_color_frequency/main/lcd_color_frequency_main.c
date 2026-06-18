/*
 * ESP-SparkBot LCD 颜色与背光频率控制示例。
 *
 * 程序初始化头部 ST7789 LCD，将屏幕填充为预设颜色，并通过 GPIO46 控制背光闪烁频率，
 * 用于观察“屏幕颜色”和“背光闪烁频率”之间的对应关系。
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

static const char *TAG = "lcd_color_freq";

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This LCD example is for the ESP-SparkBot ESP32-S3 head hardware."
#endif

/* ESP-SparkBot 头部 LCD 的硬件引脚定义，来源于 esp_sparkbot_bsp。 */
#define LCD_MOSI_GPIO             47
#define LCD_CLK_GPIO              21
#define LCD_CS_GPIO               44
#define LCD_DC_GPIO               43
#define LCD_RST_GPIO              (-1)
#define LCD_BL_GPIO               46

/*
 * LCD 使用 SPI2_HOST 与 40MHz 像素时钟。
 * 这里沿用官方 BSP 中较保守的直连屏幕配置，降低初始化失败或显示异常的概率。
 */
#define LCD_HOST                  SPI2_HOST
#define LCD_SPI_MODE              0
#define LCD_PIXEL_CLOCK_HZ        (40 * 1000 * 1000)

#define LCD_H_RES                 240
#define LCD_V_RES                 240
#define LCD_CMD_BITS              8
#define LCD_PARAM_BITS            8
#define LCD_FLUSH_LINES           10
#define LCD_TRANSFER_TIMEOUT_MS   1000

#ifndef CONFIG_LCD_COLOR_HOLD_MS
#define CONFIG_LCD_COLOR_HOLD_MS  4000
#endif

#ifndef CONFIG_LCD_MIN_HALF_PERIOD_MS
#define CONFIG_LCD_MIN_HALF_PERIOD_MS 40
#endif

/* 将 8bit RGB888 颜色压缩为 ST7789 常用的 16bit RGB565 格式。 */
#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

typedef struct {
    const char *name;       /* 日志中显示的模式名称。 */
    uint16_t color;         /* LCD 要填充的 RGB565 颜色。 */
    uint16_t frequency_hz;  /* 当前颜色下背光闪烁频率，单位 Hz。 */
} lcd_light_mode_t;

/* 颜色和背光闪烁频率的对应表，主循环会按顺序轮播这些模式。 */
static const lcd_light_mode_t s_light_modes[] = {
    { "red-1hz", RGB565(255, 0, 0), 1 },
    { "green-2hz", RGB565(0, 255, 0), 2 },
    { "blue-4hz", RGB565(0, 64, 255), 4 },
    { "yellow-6hz", RGB565(255, 210, 0), 6 },
    { "purple-8hz", RGB565(160, 32, 240), 8 },
};

static esp_lcd_panel_handle_t s_panel = NULL;
static SemaphoreHandle_t s_flush_done_sem = NULL;
/* DMA_ATTR 保证该行缓冲区可被 SPI DMA 直接访问；每次只刷 LCD_FLUSH_LINES 行以节省 RAM。 */
static DMA_ATTR uint16_t s_line_buffer[LCD_H_RES * LCD_FLUSH_LINES];

/* SPI 颜色数据发送完成后进入该回调，在 ISR 中释放信号量通知刷屏任务继续。 */
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
    /* 等待上一块颜色数据真正通过 SPI 发送完成，避免下一次刷屏覆盖 DMA 缓冲区。 */
    if (xSemaphoreTake(s_flush_done_sem, pdMS_TO_TICKS(LCD_TRANSFER_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Timed out waiting for LCD SPI transfer");
        ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
    }
}

static void lcd_fill_color(uint16_t color)
{
    /* 先把一小块行缓冲填成同一种颜色，再分块写入整个 240x240 屏幕。 */
    for (int i = 0; i < LCD_H_RES * LCD_FLUSH_LINES; i++) {
        s_line_buffer[i] = color;
    }

    for (int y = 0; y < LCD_V_RES; y += LCD_FLUSH_LINES) {
        /* 最后一块可能不足 LCD_FLUSH_LINES 行，因此需要动态计算实际行数。 */
        const int lines = (LCD_V_RES - y) < LCD_FLUSH_LINES ? (LCD_V_RES - y) : LCD_FLUSH_LINES;
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + lines, s_line_buffer));
        lcd_wait_flush_done();
    }
}

static void lcd_backlight_set(bool on)
{
    ESP_ERROR_CHECK(gpio_set_level(LCD_BL_GPIO, on ? 1 : 0));
}

static void lcd_backlight_init(void)
{
    /* 背光引脚只需要普通 GPIO 输出，高电平点亮背光。 */
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

static uint32_t frequency_to_half_period_ms(uint16_t frequency_hz)
{
    if (frequency_hz == 0) {
        /* 频率为 0 时表示不闪烁，直接保持一个颜色周期。 */
        return CONFIG_LCD_COLOR_HOLD_MS;
    }

    /* 方波闪烁需要亮/灭各占半个周期，所以半周期 = 1000 / (2 * 频率)。 */
    uint32_t half_period_ms = 1000UL / (2UL * frequency_hz);
    if (half_period_ms < CONFIG_LCD_MIN_HALF_PERIOD_MS) {
        /* 限制最小延时，避免频率过高时 FreeRTOS 调度和肉眼观察都不稳定。 */
        half_period_ms = CONFIG_LCD_MIN_HALF_PERIOD_MS;
    }
    return half_period_ms;
}

static void lcd_run_light_mode(const lcd_light_mode_t *mode)
{
    const uint32_t half_period_ms = frequency_to_half_period_ms(mode->frequency_hz);
    uint32_t elapsed_ms = 0;
    bool backlight_on = true;

    ESP_LOGI(TAG, "mode=%s color=0x%04x frequency=%uHz half_period=%lums",
             mode->name, mode->color, mode->frequency_hz, half_period_ms);

    /* 先更新屏幕颜色，再在该颜色保持时间内按设定频率翻转背光。 */
    lcd_fill_color(mode->color);

    while (elapsed_ms < CONFIG_LCD_COLOR_HOLD_MS) {
        lcd_backlight_set(backlight_on);
        backlight_on = !backlight_on;
        vTaskDelay(pdMS_TO_TICKS(half_period_ms));
        elapsed_ms += half_period_ms;
    }

    /* 离开当前模式时恢复常亮，避免切换颜色瞬间处于灭屏状态。 */
    lcd_backlight_set(true);
}

static void lcd_init(void)
{
    /* SPI 刷屏是异步传输，使用二值信号量等待每次颜色数据发送完成。 */
    s_flush_done_sem = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(s_flush_done_sem ? ESP_OK : ESP_ERR_NO_MEM);

    lcd_backlight_init();

    ESP_LOGI(TAG, "Initialize LCD SPI bus");
    /* 初始化 LCD 所在 SPI 总线；max_transfer_sz 必须覆盖单次行缓冲传输大小。 */
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
    /* panel IO 负责把 esp_lcd 的命令/颜色数据封装成 ST7789 SPI 时序。 */
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
    /* 创建 ST7789 面板驱动，RGB565 每个像素 16bit。 */
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));

    ESP_LOGI(TAG, "Reset and initialize LCD");
    /* 复位、初始化、开启反色和显示；反色设置与 SparkBot 这块屏的实际显示效果匹配。 */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
}

void app_main(void)
{
    ESP_LOGI(TAG, "LCD color and frequency control started");
    lcd_init();

    /* 永久轮播颜色模式：每种颜色保持 CONFIG_LCD_COLOR_HOLD_MS，并使用对应背光频率。 */
    while (1) {
        for (int i = 0; i < (int)(sizeof(s_light_modes) / sizeof(s_light_modes[0])); i++) {
            lcd_run_light_mode(&s_light_modes[i]);
        }
    }
}
