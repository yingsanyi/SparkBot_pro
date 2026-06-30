/*
 * ESP-SparkBot LCD Blink 实验
 *
 * 功能：初始化头部 240x240 ST7789 LCD，填充白色，周期性闪烁背光。
 * 硬件：ESP32-S3 + ST7789 SPI LCD，背光由 GPIO46 控制。
 *
 * 依赖：esp_lcd, esp_driver_gpio, esp_driver_spi
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

/* ==================== 编译期芯片检查 ==================== */
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "本项目仅支持 ESP32-S3 芯片，请使用 idf.py set-target esp32s3"
#endif

static const char *TAG = "lcd_blink";

/* ==================== 硬件引脚宏定义 ==================== */
#define LCD_MOSI      GPIO_NUM_47   /* SPI 数据线     */
#define LCD_SCLK      GPIO_NUM_21   /* SPI 时钟线     */
#define LCD_CS        GPIO_NUM_44   /* SPI 片选       */
#define LCD_DC        GPIO_NUM_43   /* 数据/命令选择  */
#define LCD_RST       -1            /* 复位引脚未连接 */
#define LCD_BL        GPIO_NUM_46   /* 背光控制 (高电平亮, 低电平灭) */

/* ==================== LCD 显示参数 ==================== */
#define LCD_H_RES     240           /* 水平分辨率 */
#define LCD_V_RES     240           /* 垂直分辨率 */
#define LCD_PIXEL_CLK (40 * 1000 * 1000)  /* 像素时钟 40 MHz */

/* 每次刷新的行数，平衡内存占用和刷新效率 */
#define FLUSH_LINES   10

/* SPI 总线句柄 */
#define LCD_SPI_HOST  SPI2_HOST

/* ==================== 全局句柄 ==================== */
static esp_lcd_panel_handle_t panel_handle = NULL;

/* 二值信号量，用于等待 SPI 传输完成 */
static SemaphoreHandle_t xfer_done_sem = NULL;

/* ==================== SPI 传输完成回调 ==================== */
/*
 * 当 esp_lcd 完成一次 draw_bitmap 的 SPI 数据传输后，
 * 在 ISR 上下文中调用此回调，释放信号量通知主任务继续。
 */
static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    BaseType_t higher_prio_woken = pdFALSE;
    xSemaphoreGiveFromISR(xfer_done_sem, &higher_prio_woken);
    /* 如果唤醒了更高优先级的任务，请求上下文切换 */
    return (higher_prio_woken == pdTRUE);
}
/* ======================================================== */

/*
 * 初始化 SPI 总线。
 * MOSI=GPIO47, SCLK=GPIO21, 仅发送模式。
 */
static void lcd_spi_init(void)
{
    spi_bus_config_t bus_cfg = {
        .sclk_io_num     = LCD_SCLK,
        .mosi_io_num     = LCD_MOSI,
        .miso_io_num     = -1,                /* 无需 MISO */
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        /* 一次最大传输量：10 行 × 240 像素 × 2 字节 */
        .max_transfer_sz = FLUSH_LINES * LCD_H_RES * 2,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI2 总线初始化成功 (MOSI=GPIO%u, SCLK=GPIO%u)",
             LCD_MOSI, LCD_SCLK);
}

/*
 * 初始化 ST7789 LCD Panel。
 * 流程：创建 SPI IO → 创建 Panel → 复位 → 初始化 → 颜色反转 → 开显示。
 *
 * ST7789 默认 RGB 排列与常见 TFT 相反，因此需要 esp_lcd_panel_invert_color(true)
 * 来获得正确的颜色显示。
 */
static void lcd_panel_init(void)
{
    /* 步骤 1：配置 SPI 通信接口 */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num   = LCD_CS,
        .dc_gpio_num   = LCD_DC,
        .spi_mode      = 0,                /* CPOL=0, CPHA=0 */
        .pclk_hz       = LCD_PIXEL_CLK,
        .trans_queue_depth = 10,
        .lcd_cmd_bits  = 8,
        .lcd_param_bits = 8,
        /* 注册传输完成回调 */
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = NULL,
    };

    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io_handle));
    ESP_LOGI(TAG, "LCD Panel IO 创建成功");

    /* 步骤 2：配置 ST7789 Panel 设备参数 */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,         /* -1 表示跳过硬件复位 */
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,              /* RGB565 */
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));
    ESP_LOGI(TAG, "ST7789 Panel 创建成功");

    /* 步骤 3：Panel 初始化序列 */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_LOGI(TAG, "Panel 复位完成");

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_LOGI(TAG, "Panel 初始化完成");

    /* 步骤 4：颜色反转（ST7789 标准初始化后需反转颜色） */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_LOGI(TAG, "颜色反转已启用");

    /* 步骤 5：打开显示 */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_LOGI(TAG, "显示已开启");
}

/*
 * 将整屏填充为白色。
 *
 * 使用 DMA_ATTR 行缓冲区，每次刷 10 行，配合信号量等待传输完成，
 * 避免 DMA 传输冲突和帧撕裂。
 */
static void lcd_fill_white(void)
{
    /* DMA_ATTR 确保缓冲区放置在 DMA 可访问的内存区域 */
    static DMA_ATTR uint16_t line_buf[LCD_H_RES * FLUSH_LINES];

    /* 将缓冲区全部填充为白色 (RGB565 全 1 = 0xFFFF) */
    for (int i = 0; i < LCD_H_RES * FLUSH_LINES; i++) {
        line_buf[i] = 0xFFFF;
    }

    /* 逐段刷新：每次刷 FLUSH_LINES 行 */
    for (int y = 0; y < LCD_V_RES; y += FLUSH_LINES) {
        int actual_lines = (y + FLUSH_LINES <= LCD_V_RES) ? FLUSH_LINES : (LCD_V_RES - y);

        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle,
                                                  0, y,
                                                  LCD_H_RES, y + actual_lines,
                                                  line_buf));

        /* 等待本次 SPI 传输完成（信号量由 on_color_trans_done 释放） */
        if (xSemaphoreTake(xfer_done_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGW(TAG, "SPI 传输超时 @ y=%d", y);
        }
    }

    ESP_LOGI(TAG, "整屏已填充为白色");
}

/*
 * 初始化背光控制 GPIO。
 * GPIO46 推挽输出，初始状态为低电平（背光灭）。
 */
static void lcd_backlight_init(void)
{
    gpio_config_t bl_conf = {
        .pin_bit_mask = BIT64(LCD_BL),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_conf);
    gpio_set_level(LCD_BL, 0);   /* 初始状态：背光灭 */
    ESP_LOGI(TAG, "背光 GPIO (GPIO%u) 初始化完成，初始状态：关闭", LCD_BL);
}

/* ==================== 主函数 ==================== */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP-SparkBot LCD Blink 实验");
    ESP_LOGI(TAG, "  芯片: ESP32-S3");
    ESP_LOGI(TAG, "  LCD:  ST7789 240x240 SPI");
    ESP_LOGI(TAG, "========================================");

    /* 创建二值信号量，用于等待 SPI 传输完成 */
    xfer_done_sem = xSemaphoreCreateBinary();
    assert(xfer_done_sem != NULL);

    /* 步骤 1：初始化 SPI 总线 */
    lcd_spi_init();

    /* 步骤 2：初始化 ST7789 Panel */
    lcd_panel_init();

    /* 步骤 3：整屏填充白色 */
    lcd_fill_white();

    /* 步骤 4：初始化背光 GPIO */
    lcd_backlight_init();

    /* 步骤 5：进入背光闪烁主循环 */
    /*
     * 闪烁周期由 Kconfig 配置项 CONFIG_LCD_BLINK_PERIOD_MS 决定。
     * 例如默认值 1000 ms，则每 500 ms 切换一次背光状态。
     */
    uint32_t period_ms = CONFIG_LCD_BLINK_PERIOD_MS;
    uint32_t half_ms   = period_ms / 2;

    ESP_LOGI(TAG, "背光闪烁周期: %lu ms (亮 %lu ms / 灭 %lu ms)",
             period_ms, half_ms, half_ms);

    while (true) {
        gpio_set_level(LCD_BL, 1);   /* 背光亮 */
        ESP_LOGI(TAG, "背光: 亮");
        vTaskDelay(pdMS_TO_TICKS(half_ms));

        gpio_set_level(LCD_BL, 0);   /* 背光灭 */
        ESP_LOGI(TAG, "背光: 灭");
        vTaskDelay(pdMS_TO_TICKS(half_ms));
    }
}
