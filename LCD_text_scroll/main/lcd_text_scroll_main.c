/*
 * ESP-SparkBot LCD marquee text demo.
 *
 * This project keeps the direct LCD initialization from LCD_blink and scrolls
 * a mixed English/Chinese message from right to left.
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

static const char *TAG = "lcd_scroll";

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This LCD demo is for the ESP-SparkBot ESP32-S3 head hardware."
#endif

#define LCD_MOSI_GPIO                  47
#define LCD_CLK_GPIO                   21
#define LCD_CS_GPIO                    44
#define LCD_DC_GPIO                    43
#define LCD_RST_GPIO                   (-1)
#define LCD_BL_GPIO                    46

#define LCD_HOST                       SPI2_HOST
#define LCD_SPI_MODE                   0
#define LCD_PIXEL_CLOCK_HZ             (40 * 1000 * 1000)

#define LCD_H_RES                      240
#define LCD_V_RES                      240
#define LCD_CMD_BITS                   8
#define LCD_PARAM_BITS                 8
#define LCD_FLUSH_LINES                10
#define LCD_TRANSFER_TIMEOUT_MS        1000

#ifndef CONFIG_LCD_TEXT_SCROLL_FRAME_MS
#define CONFIG_LCD_TEXT_SCROLL_FRAME_MS 40
#endif

#ifndef CONFIG_LCD_TEXT_SCROLL_PIXELS_PER_FRAME
#define CONFIG_LCD_TEXT_SCROLL_PIXELS_PER_FRAME 2
#endif

#define LCD_RGB565_BLACK               0x0000
#define LCD_RGB565_WHITE               0xFFFF

#define ASCII_GLYPH_W                  5
#define ASCII_GLYPH_H                  7
#define ASCII_SCALE                    5
#define ASCII_TEXT_HEIGHT              (ASCII_GLYPH_H * ASCII_SCALE)

#define CHINESE_GLYPH_SIZE             40
#define CHINESE_GLYPH_COUNT            4
#define CHINESE_GLYPH_GAP              8

#define FULLWIDTH_PUNCT_W              28
#define MESSAGE_SPACE_W                16
#define MESSAGE_ASCII_PUNCT_GAP        6
#define MESSAGE_TOP_Y                  ((LCD_V_RES - CHINESE_GLYPH_SIZE) / 2)
#define MESSAGE_ASCII_Y                (MESSAGE_TOP_Y + ((CHINESE_GLYPH_SIZE - ASCII_TEXT_HEIGHT) / 2))

static esp_lcd_panel_handle_t s_panel = NULL;
static SemaphoreHandle_t s_flush_done_sem = NULL;
static DMA_ATTR uint16_t s_line_buffer[LCD_H_RES * LCD_FLUSH_LINES];

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
    if (xSemaphoreTake(s_flush_done_sem, pdMS_TO_TICKS(LCD_TRANSFER_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Timed out waiting for LCD SPI transfer");
        ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
    }
}

static void lcd_backlight_set(bool on)
{
    ESP_ERROR_CHECK(gpio_set_level(LCD_BL_GPIO, on ? 1 : 0));
}

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

static void lcd_init(void)
{
    s_flush_done_sem = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(s_flush_done_sem ? ESP_OK : ESP_ERR_NO_MEM);

    lcd_backlight_init();

    ESP_LOGI(TAG, "Initialize LCD SPI bus");
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
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));

    ESP_LOGI(TAG, "Reset and initialize LCD");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
}

static void fill_strip(int lines, uint16_t color)
{
    for (int i = 0; i < LCD_H_RES * lines; i++) {
        s_line_buffer[i] = color;
    }
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void draw_rect_to_strip(int strip_y, int strip_lines, int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    const int x0 = clamp_int(x, 0, LCD_H_RES);
    const int x1 = clamp_int(x + w, 0, LCD_H_RES);
    const int y0 = clamp_int(y, strip_y, strip_y + strip_lines);
    const int y1 = clamp_int(y + h, strip_y, strip_y + strip_lines);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int row = y0; row < y1; row++) {
        uint16_t *line = &s_line_buffer[(row - strip_y) * LCD_H_RES];
        for (int col = x0; col < x1; col++) {
            line[col] = color;
        }
    }
}

static const uint8_t *ascii_glyph_rows(char ch)
{
    static const uint8_t space[ASCII_GLYPH_H] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    static const uint8_t glyph_h[ASCII_GLYPH_H] = {
        0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11,
    };
    static const uint8_t glyph_e[ASCII_GLYPH_H] = {
        0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00,
    };
    static const uint8_t glyph_l[ASCII_GLYPH_H] = {
        0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00,
    };
    static const uint8_t glyph_o[ASCII_GLYPH_H] = {
        0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00,
    };
    static const uint8_t glyph_w[ASCII_GLYPH_H] = {
        0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A,
    };
    static const uint8_t glyph_r[ASCII_GLYPH_H] = {
        0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00,
    };
    static const uint8_t glyph_d[ASCII_GLYPH_H] = {
        0x01, 0x01, 0x0D, 0x13, 0x11, 0x13, 0x0D,
    };

    switch (ch) {
    case 'H':
        return glyph_h;
    case 'e':
        return glyph_e;
    case 'l':
        return glyph_l;
    case 'o':
        return glyph_o;
    case 'W':
        return glyph_w;
    case 'r':
        return glyph_r;
    case 'd':
        return glyph_d;
    default:
        return space;
    }
}

static int ascii_text_width(const char *text, int scale)
{
    int len = 0;
    while (text[len] != '\0') {
        len++;
    }

    if (len == 0) {
        return 0;
    }
    return (len * ASCII_GLYPH_W * scale) + ((len - 1) * scale);
}

static void draw_ascii_text_to_strip(int strip_y,
                                     int strip_lines,
                                     const char *text,
                                     int x,
                                     int y,
                                     int scale,
                                     uint16_t color)
{
    int cursor_x = x;
    for (int i = 0; text[i] != '\0'; i++) {
        const uint8_t *rows = ascii_glyph_rows(text[i]);
        for (int row = 0; row < ASCII_GLYPH_H; row++) {
            for (int col = 0; col < ASCII_GLYPH_W; col++) {
                if (rows[row] & (1U << (ASCII_GLYPH_W - 1 - col))) {
                    draw_rect_to_strip(strip_y,
                                       strip_lines,
                                       cursor_x + col * scale,
                                       y + row * scale,
                                       scale,
                                       scale,
                                       color);
                }
            }
        }
        cursor_x += (ASCII_GLYPH_W + 1) * scale;
    }
}

/*
 * Chinese glyph bitmaps for the four characters in the message. They follow
 * the same approach as the SparkBot LVGL examples: use pregenerated glyph
 * rows from a real font, then draw them directly through esp_lcd.
 */
static const uint64_t s_chinese_glyph_rows[CHINESE_GLYPH_COUNT][CHINESE_GLYPH_SIZE] = {
    { /* ni */
        0x0000000000ULL,
        0x0000000000ULL,
        0x001C1C0000ULL,
        0x001C1C0000ULL,
        0x001C380000ULL,
        0x0038380000ULL,
        0x0038380000ULL,
        0x00387FFFE0ULL,
        0x00707FFFE0ULL,
        0x0070E000E0ULL,
        0x00F1E000E0ULL,
        0x00F1C001C0ULL,
        0x01F381C1C0ULL,
        0x01F381C380ULL,
        0x03F101C380ULL,
        0x07F001C000ULL,
        0x077021C200ULL,
        0x0E7071C600ULL,
        0x0E7071C700ULL,
        0x047071C700ULL,
        0x0070E1C380ULL,
        0x0070E1C380ULL,
        0x0071C1C1C0ULL,
        0x0071C1C1C0ULL,
        0x007381C0E0ULL,
        0x007381C0E0ULL,
        0x007701C0E0ULL,
        0x007101C040ULL,
        0x007001C000ULL,
        0x007003C000ULL,
        0x00707F8000ULL,
        0x00703F8000ULL,
        0x0060000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
    },
    { /* hao */
        0x0000000000ULL,
        0x0000000000ULL,
        0x0060000000ULL,
        0x00E0000000ULL,
        0x00E07FFFC0ULL,
        0x00E07FFFC0ULL,
        0x00E00007C0ULL,
        0x00E0000780ULL,
        0x0FFF001F00ULL,
        0x0FFF003C00ULL,
        0x0FFF007800ULL,
        0x01C3007000ULL,
        0x01C3007000ULL,
        0x01C7007000ULL,
        0x0187007000ULL,
        0x038700F000ULL,
        0x0387FFFFF0ULL,
        0x0386FFFFF0ULL,
        0x030E00F000ULL,
        0x078E007000ULL,
        0x03CC007000ULL,
        0x01FC007000ULL,
        0x007C007000ULL,
        0x003C007000ULL,
        0x007E007000ULL,
        0x007F807000ULL,
        0x00E7807000ULL,
        0x01C3007000ULL,
        0x03C000F000ULL,
        0x07801FE000ULL,
        0x0E001FE000ULL,
        0x04001FC000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
    },
    { /* shi */
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000700C00ULL,
        0x0000700E00ULL,
        0x00E0700E00ULL,
        0x00E0700E00ULL,
        0x00E0700E00ULL,
        0x00E0700E00ULL,
        0x0060700E00ULL,
        0x00F0701E00ULL,
        0x0FFFFFFFE0ULL,
        0x0FFFFFFFE0ULL,
        0x00F0701E00ULL,
        0x0060700E00ULL,
        0x00E0700E00ULL,
        0x00E0700E00ULL,
        0x00E0700E00ULL,
        0x00E0700E00ULL,
        0x00E0700E00ULL,
        0x00E0700E00ULL,
        0x00E07FFE00ULL,
        0x00E07FFE00ULL,
        0x00E07FFE00ULL,
        0x00E0700E00ULL,
        0x00E0700C00ULL,
        0x00E0000000ULL,
        0x00E0000000ULL,
        0x00E0000000ULL,
        0x00F0000000ULL,
        0x00FFFFFFC0ULL,
        0x00FFFFFFC0ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
    },
    { /* jie */
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x01FFFFFF00ULL,
        0x01FFFFFF00ULL,
        0x01C0380700ULL,
        0x01C0380300ULL,
        0x01C0380700ULL,
        0x01C0380700ULL,
        0x01FFFFFF00ULL,
        0x01FFFFFF00ULL,
        0x01C0380700ULL,
        0x01C0380300ULL,
        0x01C0380700ULL,
        0x01C0380700ULL,
        0x01FFFFFF00ULL,
        0x01FFFFFF00ULL,
        0x01C1FF0700ULL,
        0x0003C38000ULL,
        0x000781E000ULL,
        0x001E00F800ULL,
        0x007E00FE00ULL,
        0x01F700EFC0ULL,
        0x07C700E7E0ULL,
        0x078600E1C0ULL,
        0x020E00E000ULL,
        0x000E00E000ULL,
        0x001E00E000ULL,
        0x003C00E000ULL,
        0x007800E000ULL,
        0x01F000E000ULL,
        0x01E000E000ULL,
        0x0080000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
        0x0000000000ULL,
    },
};

static void draw_chinese_glyph_to_strip(int strip_y,
                                        int strip_lines,
                                        int glyph_index,
                                        int x,
                                        int y,
                                        uint16_t color)
{
    if (glyph_index < 0 || glyph_index >= CHINESE_GLYPH_COUNT) {
        return;
    }

    for (int row = 0; row < CHINESE_GLYPH_SIZE; row++) {
        const int screen_y = y + row;
        if (screen_y < strip_y || screen_y >= strip_y + strip_lines) {
            continue;
        }
        if (screen_y < 0 || screen_y >= LCD_V_RES) {
            continue;
        }

        uint16_t *line = &s_line_buffer[(screen_y - strip_y) * LCD_H_RES];
        const uint64_t bits = s_chinese_glyph_rows[glyph_index][row];
        for (int col = 0; col < CHINESE_GLYPH_SIZE; col++) {
            const int screen_x = x + col;
            if (screen_x < 0 || screen_x >= LCD_H_RES) {
                continue;
            }
            if (bits & (1ULL << (CHINESE_GLYPH_SIZE - 1 - col))) {
                line[screen_x] = color;
            }
        }
    }
}

static void draw_fullwidth_comma_to_strip(int strip_y, int strip_lines, int x, int y, uint16_t color)
{
    draw_rect_to_strip(strip_y, strip_lines, x + 13, y + 27, 7, 6, color);
    draw_rect_to_strip(strip_y, strip_lines, x + 12, y + 33, 5, 4, color);
    draw_rect_to_strip(strip_y, strip_lines, x + 10, y + 36, 4, 3, color);
}

static void draw_fullwidth_exclamation_to_strip(int strip_y, int strip_lines, int x, int y, uint16_t color)
{
    draw_rect_to_strip(strip_y, strip_lines, x + 13, y + 6, 5, 22, color);
    draw_rect_to_strip(strip_y, strip_lines, x + 12, y + 31, 7, 6, color);
}

static int marquee_text_width(void)
{
    const char *ascii_text = "HelloWorld";
    const int chinese_width = CHINESE_GLYPH_COUNT * CHINESE_GLYPH_SIZE +
                              (CHINESE_GLYPH_COUNT - 1) * CHINESE_GLYPH_GAP;

    return ascii_text_width(ascii_text, ASCII_SCALE) +
           MESSAGE_ASCII_PUNCT_GAP +
           FULLWIDTH_PUNCT_W +
           MESSAGE_SPACE_W +
           chinese_width +
           CHINESE_GLYPH_GAP +
           FULLWIDTH_PUNCT_W;
}

static void draw_marquee_text_to_strip(int strip_y, int strip_lines, int start_x, uint16_t color)
{
    const char *ascii_text = "HelloWorld";
    int cursor_x = start_x;

    draw_ascii_text_to_strip(strip_y, strip_lines, ascii_text, cursor_x, MESSAGE_ASCII_Y, ASCII_SCALE, color);
    cursor_x += ascii_text_width(ascii_text, ASCII_SCALE) + MESSAGE_ASCII_PUNCT_GAP;

    draw_fullwidth_comma_to_strip(strip_y, strip_lines, cursor_x, MESSAGE_TOP_Y, color);
    cursor_x += FULLWIDTH_PUNCT_W + MESSAGE_SPACE_W;

    for (int glyph = 0; glyph < CHINESE_GLYPH_COUNT; glyph++) {
        draw_chinese_glyph_to_strip(strip_y, strip_lines, glyph, cursor_x, MESSAGE_TOP_Y, color);
        cursor_x += CHINESE_GLYPH_SIZE;
        if (glyph != CHINESE_GLYPH_COUNT - 1) {
            cursor_x += CHINESE_GLYPH_GAP;
        }
    }

    cursor_x += CHINESE_GLYPH_GAP;
    draw_fullwidth_exclamation_to_strip(strip_y, strip_lines, cursor_x, MESSAGE_TOP_Y, color);
}

static void lcd_draw_marquee_frame(int start_x)
{
    const uint16_t bg = LCD_RGB565_WHITE;
    const uint16_t fg = LCD_RGB565_BLACK;

    for (int y = 0; y < LCD_V_RES; y += LCD_FLUSH_LINES) {
        const int lines = (LCD_V_RES - y) < LCD_FLUSH_LINES ? (LCD_V_RES - y) : LCD_FLUSH_LINES;
        fill_strip(lines, bg);
        draw_marquee_text_to_strip(y, lines, start_x, fg);

        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + lines, s_line_buffer));
        lcd_wait_flush_done();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "LCD text scroll started");
    lcd_init();

    const int message_width = marquee_text_width();
    const int reset_distance = LCD_H_RES + message_width;
    int scroll_offset = 0;

    ESP_LOGI(TAG, "Scroll message width: %d px", message_width);

    while (1) {
        lcd_draw_marquee_frame(LCD_H_RES - scroll_offset);

        scroll_offset += CONFIG_LCD_TEXT_SCROLL_PIXELS_PER_FRAME;
        if (scroll_offset > reset_distance) {
            scroll_offset = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_LCD_TEXT_SCROLL_FRAME_MS));
    }
}
