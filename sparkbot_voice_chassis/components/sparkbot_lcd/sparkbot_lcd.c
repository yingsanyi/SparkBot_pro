#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sparkbot_lcd.h"

static const char *TAG = "sparkbot_lcd";

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

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define COLOR_BLACK                    RGB565(0, 0, 0)
#define COLOR_WHITE                    RGB565(255, 255, 255)
#define COLOR_FACE_BG                  RGB565(3, 7, 10)
#define COLOR_FACE_DIM                 RGB565(70, 84, 96)
#define COLOR_EYE_SHADOW               RGB565(24, 36, 46)
#define COLOR_EYE_BLUE                 RGB565(42, 160, 230)
#define COLOR_EYE_GOLD                 RGB565(244, 188, 55)
#define COLOR_EYE_GREEN                RGB565(55, 210, 124)
#define COLOR_EYE_RED                  RGB565(230, 64, 70)
#define COLOR_TEXT_DIM                 RGB565(112, 138, 150)

static esp_lcd_panel_handle_t s_panel = NULL;
static SemaphoreHandle_t s_flush_done_sem = NULL;
static SemaphoreHandle_t s_lcd_mutex = NULL;
static QueueHandle_t s_lcd_queue = NULL;
static DMA_ATTR uint16_t s_line_buffer[LCD_H_RES * LCD_FLUSH_LINES];

typedef struct {
    sparkbot_lcd_state_t state;
    char line1[32];
    char line2[32];
} sparkbot_lcd_event_t;

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

static void fill_strip(int lines, uint16_t color)
{
    for (int i = 0; i < LCD_H_RES * lines; i++) {
        s_line_buffer[i] = color;
    }
}

static void draw_disc_to_strip(int strip_y,
                               int strip_lines,
                               int cx,
                               int cy,
                               int radius,
                               uint16_t color)
{
    const int r2 = radius * radius;
    for (int screen_y = cy - radius; screen_y <= cy + radius; screen_y++) {
        if (screen_y < strip_y || screen_y >= strip_y + strip_lines || screen_y < 0 || screen_y >= LCD_V_RES) {
            continue;
        }
        uint16_t *line = &s_line_buffer[(screen_y - strip_y) * LCD_H_RES];
        const int dy = screen_y - cy;
        for (int screen_x = cx - radius; screen_x <= cx + radius; screen_x++) {
            const int dx = screen_x - cx;
            if (screen_x >= 0 && screen_x < LCD_H_RES && dx * dx + dy * dy <= r2) {
                line[screen_x] = color;
            }
        }
    }
}

static void draw_line_to_strip(int strip_y,
                               int strip_lines,
                               int x0,
                               int y0,
                               int x1,
                               int y1,
                               int thickness,
                               uint16_t color)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        draw_disc_to_strip(strip_y, strip_lines, x0, y0, thickness, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static bool ellipse_contains(int x, int y, int cx, int cy, int rx, int ry)
{
    const int64_t dx = x - cx;
    const int64_t dy = y - cy;
    const int64_t lhs = dx * dx * ry * ry + dy * dy * rx * rx;
    const int64_t rhs = (int64_t)rx * rx * ry * ry;
    return lhs <= rhs;
}

static void draw_eye_to_strip(int strip_y,
                              int strip_lines,
                              int cx,
                              int cy,
                              int rx,
                              int ry,
                              int pupil_dx,
                              int openness,
                              bool lashes,
                              uint16_t iris_color)
{
    const int effective_ry = (ry * openness) / 100;
    if (effective_ry <= 3) {
        draw_line_to_strip(strip_y, strip_lines, cx - rx + 4, cy, cx + rx - 4, cy, 2, iris_color);
        return;
    }

    for (int screen_y = cy - effective_ry - 3; screen_y <= cy + effective_ry + 3; screen_y++) {
        if (screen_y < strip_y || screen_y >= strip_y + strip_lines || screen_y < 0 || screen_y >= LCD_V_RES) {
            continue;
        }
        uint16_t *line = &s_line_buffer[(screen_y - strip_y) * LCD_H_RES];
        for (int screen_x = cx - rx - 3; screen_x <= cx + rx + 3; screen_x++) {
            if (screen_x < 0 || screen_x >= LCD_H_RES) {
                continue;
            }

            const bool outer = ellipse_contains(screen_x, screen_y, cx, cy, rx, effective_ry);
            const bool inner = ellipse_contains(screen_x, screen_y, cx, cy, rx - 4, effective_ry - 4);
            if (outer) {
                line[screen_x] = inner ? COLOR_WHITE : iris_color;
            }
        }
    }

    draw_disc_to_strip(strip_y, strip_lines, cx + pupil_dx, cy, effective_ry > 17 ? 16 : 11, COLOR_BLACK);
    draw_disc_to_strip(strip_y, strip_lines, cx + pupil_dx + 5, cy - 6, 4, COLOR_WHITE);

    if (lashes) {
        draw_line_to_strip(strip_y, strip_lines, cx - rx + 10, cy - effective_ry + 7, cx - rx - 3, cy - effective_ry - 7, 1, iris_color);
        draw_line_to_strip(strip_y, strip_lines, cx - rx + 24, cy - effective_ry + 2, cx - rx + 17, cy - effective_ry - 13, 1, iris_color);
        draw_line_to_strip(strip_y, strip_lines, cx + rx - 10, cy - effective_ry + 7, cx + rx + 3, cy - effective_ry - 7, 1, iris_color);
        draw_line_to_strip(strip_y, strip_lines, cx + rx - 24, cy - effective_ry + 2, cx + rx - 17, cy - effective_ry - 13, 1, iris_color);
    }
}

static uint8_t glyph5x7(char c, int row)
{
    static const uint8_t digits[10][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    };

    static const uint8_t letters[26][7] = {
        {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
        {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // B
        {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // C
        {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // D
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
        {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, // G
        {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // H
        {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // I
        {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}, // J
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
        {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
        {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // N
        {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // O
        {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // P
        {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // Q
        {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // R
        {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // S
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
        {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04}, // V
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}, // W
        {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
        {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // Y
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, // Z
    };

    if (row < 0 || row >= 7) {
        return 0;
    }
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'][row];
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'][row];
    }

    switch (c) {
    case ':':
        return (row == 1 || row == 5) ? 0x04 : 0;
    case '-':
        return row == 3 ? 0x1F : 0;
    case '.':
        return row == 6 ? 0x04 : 0;
    case '/':
        return (uint8_t)(0x01 << (6 - row < 5 ? 6 - row : 0));
    case ' ':
    default:
        return 0;
    }
}

static int text_width_px(const char *text, int scale)
{
    if (!text) {
        return 0;
    }

    int width = 0;
    for (const char *p = text; *p; p++) {
        width += 6 * scale;
    }
    return width > 0 ? width - scale : 0;
}

static void draw_text_to_strip(int strip_y,
                               int strip_lines,
                               const char *text,
                               int x,
                               int y,
                               int scale,
                               uint16_t color)
{
    if (!text || scale <= 0) {
        return;
    }

    int cursor_x = x;
    for (const char *p = text; *p; p++) {
        for (int glyph_row = 0; glyph_row < 7; glyph_row++) {
            const uint8_t bits = glyph5x7(*p, glyph_row);
            for (int sy = 0; sy < scale; sy++) {
                const int screen_y = y + glyph_row * scale + sy;
                if (screen_y < strip_y || screen_y >= strip_y + strip_lines || screen_y < 0 || screen_y >= LCD_V_RES) {
                    continue;
                }

                uint16_t *line = &s_line_buffer[(screen_y - strip_y) * LCD_H_RES];
                for (int glyph_col = 0; glyph_col < 5; glyph_col++) {
                    if (!(bits & (1U << (4 - glyph_col)))) {
                        continue;
                    }
                    for (int sx = 0; sx < scale; sx++) {
                        const int screen_x = cursor_x + glyph_col * scale + sx;
                        if (screen_x >= 0 && screen_x < LCD_H_RES) {
                            line[screen_x] = color;
                        }
                    }
                }
            }
        }
        cursor_x += 6 * scale;
    }
}

static void draw_centered_text_to_strip(int strip_y,
                                        int strip_lines,
                                        const char *text,
                                        int y,
                                        int scale,
                                        uint16_t color)
{
    const int x = (LCD_H_RES - text_width_px(text, scale)) / 2;
    draw_text_to_strip(strip_y, strip_lines, text, x, y, scale, color);
}

static void draw_face_to_strip(int strip_y,
                               int strip_lines,
                               sparkbot_lcd_state_t state,
                               const char *line1,
                               const char *line2)
{
    int openness = 88;
    int pupil_dx = 0;
    uint16_t iris = COLOR_EYE_BLUE;
    const char *caption = line1;
    const char *subcaption = line2;
    bool lashes = true;

    switch (state) {
    case SPARKBOT_LCD_STATE_BOOT:
        openness = 46;
        iris = COLOR_FACE_DIM;
        caption = line1 ? line1 : "BOOT";
        subcaption = line2 ? line2 : "";
        break;
    case SPARKBOT_LCD_STATE_IDLE:
        openness = 62;
        iris = COLOR_EYE_BLUE;
        caption = line1 ? line1 : "HI LEXIN";
        subcaption = line2 ? line2 : "";
        break;
    case SPARKBOT_LCD_STATE_WAKE:
        openness = 100;
        pupil_dx = -5;
        iris = COLOR_EYE_GOLD;
        caption = line1 ? line1 : "ASK";
        subcaption = line2 ? line2 : "";
        break;
    case SPARKBOT_LCD_STATE_LISTENING:
        openness = 94;
        pupil_dx = 4;
        iris = COLOR_EYE_BLUE;
        caption = line1 ? line1 : "LISTEN";
        subcaption = line2 ? line2 : "";
        break;
    case SPARKBOT_LCD_STATE_RUNNING:
        openness = 100;
        pupil_dx = 0;
        iris = COLOR_EYE_GREEN;
        caption = line1 ? line1 : "OK";
        subcaption = line2 ? line2 : "";
        break;
    case SPARKBOT_LCD_STATE_TIMEOUT:
        openness = 24;
        iris = COLOR_FACE_DIM;
        caption = line1 ? line1 : "TIMEOUT";
        subcaption = line2 ? line2 : "";
        break;
    case SPARKBOT_LCD_STATE_ERROR:
    default:
        openness = 72;
        iris = COLOR_EYE_RED;
        caption = line1 ? line1 : "ERROR";
        subcaption = line2 ? line2 : "";
        break;
    }

    draw_disc_to_strip(strip_y, strip_lines, 60, 74, 32, COLOR_EYE_SHADOW);
    draw_disc_to_strip(strip_y, strip_lines, 180, 74, 32, COLOR_EYE_SHADOW);
    draw_eye_to_strip(strip_y, strip_lines, 60, 86, 42, 28, pupil_dx, openness, lashes, iris);
    draw_eye_to_strip(strip_y, strip_lines, 180, 86, 42, 28, pupil_dx, openness, lashes, iris);

    if (state == SPARKBOT_LCD_STATE_RUNNING) {
        draw_line_to_strip(strip_y, strip_lines, 86, 156, 106, 170, 2, COLOR_EYE_GREEN);
        draw_line_to_strip(strip_y, strip_lines, 106, 170, 154, 142, 2, COLOR_EYE_GREEN);
    } else if (state == SPARKBOT_LCD_STATE_TIMEOUT) {
        draw_line_to_strip(strip_y, strip_lines, 92, 158, 148, 158, 2, COLOR_FACE_DIM);
    } else if (state == SPARKBOT_LCD_STATE_ERROR) {
        draw_line_to_strip(strip_y, strip_lines, 96, 144, 144, 176, 2, COLOR_EYE_RED);
        draw_line_to_strip(strip_y, strip_lines, 144, 144, 96, 176, 2, COLOR_EYE_RED);
    } else {
        draw_line_to_strip(strip_y, strip_lines, 88, 158, 108, 170, 2, iris);
        draw_line_to_strip(strip_y, strip_lines, 108, 170, 132, 170, 2, iris);
        draw_line_to_strip(strip_y, strip_lines, 132, 170, 152, 158, 2, iris);
    }

    draw_centered_text_to_strip(strip_y, strip_lines, caption, 198, 2, COLOR_WHITE);
    if (subcaption && subcaption[0]) {
        draw_centered_text_to_strip(strip_y, strip_lines, subcaption, 220, 1, COLOR_TEXT_DIM);
    }
}

static void lcd_render_state(sparkbot_lcd_state_t state, const char *line1, const char *line2)
{
    if (!s_panel || !s_lcd_mutex) {
        return;
    }

    xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);

    for (int y = 0; y < LCD_V_RES; y += LCD_FLUSH_LINES) {
        const int lines = (LCD_V_RES - y) < LCD_FLUSH_LINES ? (LCD_V_RES - y) : LCD_FLUSH_LINES;
        fill_strip(lines, COLOR_FACE_BG);
        draw_face_to_strip(y, lines, state, line1, line2);

        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + lines, s_line_buffer));
        lcd_wait_flush_done();
    }

    xSemaphoreGive(s_lcd_mutex);
}

static void lcd_event_task(void *arg)
{
    (void)arg;

    while (true) {
        sparkbot_lcd_event_t event = {0};
        if (xQueueReceive(s_lcd_queue, &event, portMAX_DELAY) == pdTRUE) {
            lcd_render_state(event.state,
                             event.line1[0] ? event.line1 : NULL,
                             event.line2[0] ? event.line2 : NULL);
        }
    }
}

void sparkbot_lcd_show_state(sparkbot_lcd_state_t state, const char *line1, const char *line2)
{
    lcd_render_state(state, line1, line2);
}

void sparkbot_lcd_post_state(sparkbot_lcd_state_t state, const char *line1, const char *line2)
{
    if (!s_lcd_queue) {
        sparkbot_lcd_show_state(state, line1, line2);
        return;
    }

    sparkbot_lcd_event_t event = {
        .state = state,
    };
    if (line1) {
        snprintf(event.line1, sizeof(event.line1), "%s", line1);
    }
    if (line2) {
        snprintf(event.line2, sizeof(event.line2), "%s", line2);
    }
    xQueueSend(s_lcd_queue, &event, pdMS_TO_TICKS(20));
}

esp_err_t sparkbot_lcd_init(void)
{
    if (s_panel) {
        return ESP_OK;
    }

    s_flush_done_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_flush_done_sem, ESP_ERR_NO_MEM, TAG, "create flush semaphore failed");

    s_lcd_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lcd_mutex, ESP_ERR_NO_MEM, TAG, "create LCD mutex failed");

    s_lcd_queue = xQueueCreate(6, sizeof(sparkbot_lcd_event_t));
    ESP_RETURN_ON_FALSE(s_lcd_queue, ESP_ERR_NO_MEM, TAG, "create LCD queue failed");

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
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG, "SPI bus init failed");

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
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle),
                        TAG, "panel IO init failed");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel),
                        TAG, "ST7789 init failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "panel invert failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "panel on failed");

    sparkbot_lcd_show_state(SPARKBOT_LCD_STATE_BOOT, "BOOTING", "VOICE CHASSIS");
    BaseType_t task_ret = xTaskCreatePinnedToCore(lcd_event_task, "lcd_event", 3 * 1024, NULL, 2, NULL, 1);
    ESP_RETURN_ON_FALSE(task_ret == pdPASS, ESP_FAIL, TAG, "create LCD task failed");
    return ESP_OK;
}
