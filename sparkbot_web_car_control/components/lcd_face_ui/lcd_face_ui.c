#include <stdbool.h>
#include <stdint.h>

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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lcd_face_ui.h"

static const char *TAG = "lcd_face_ui";

#define LCD_MOSI_GPIO             47
#define LCD_CLK_GPIO              21
#define LCD_CS_GPIO               44
#define LCD_DC_GPIO               43
#define LCD_RST_GPIO              (-1)
#define LCD_BL_GPIO               46

#define LCD_HOST                  SPI2_HOST
#define LCD_SPI_MODE              0
#define LCD_PIXEL_CLOCK_HZ        (40 * 1000 * 1000)

#define LCD_H_RES                 240
#define LCD_V_RES                 240
#define LCD_CMD_BITS              8
#define LCD_PARAM_BITS            8
#define LCD_FLUSH_LINES           10
#define LCD_TRANSFER_TIMEOUT_MS    1000

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define COLOR_BG                  RGB565(8, 14, 18)
#define COLOR_PANEL               RGB565(22, 34, 42)
#define COLOR_SHADOW              RGB565(18, 28, 36)
#define COLOR_WHITE               RGB565(255, 255, 255)
#define COLOR_DIM                 RGB565(102, 120, 132)
#define COLOR_BLUE                RGB565(44, 156, 232)
#define COLOR_GOLD                RGB565(246, 188, 58)
#define COLOR_GREEN               RGB565(56, 210, 126)
#define COLOR_RED                 RGB565(232, 72, 78)
#define COLOR_CYAN                RGB565(68, 214, 220)
#define COLOR_ORANGE              RGB565(246, 146, 58)

typedef enum {
    MOUTH_LINE = 0,
    MOUTH_SMILE,
    MOUTH_OPEN,
    MOUTH_FROWN,
    MOUTH_SMALL,
} mouth_kind_t;

typedef struct {
    int left_open;
    int right_open;
    int left_pupil_dx;
    int right_pupil_dx;
    bool left_lashes;
    bool right_lashes;
    bool show_brows;
    int brow_lx0;
    int brow_ly0;
    int brow_lx1;
    int brow_ly1;
    int brow_rx0;
    int brow_ry0;
    int brow_rx1;
    int brow_ry1;
    mouth_kind_t mouth;
    uint16_t iris_color;
    uint16_t brow_color;
    uint16_t mouth_color;
    const char *line1;
    const char *line2;
} face_style_t;

static esp_lcd_panel_handle_t s_panel = NULL;
static SemaphoreHandle_t s_flush_done_sem = NULL;
static SemaphoreHandle_t s_lcd_mutex = NULL;
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

static bool lcd_wait_flush_done(void)
{
    if (!s_flush_done_sem) {
        return false;
    }
    if (xSemaphoreTake(s_flush_done_sem, pdMS_TO_TICKS(LCD_TRANSFER_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Timed out waiting for LCD SPI transfer");
        return false;
    }
    return true;
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

    draw_disc_to_strip(strip_y, strip_lines, cx + pupil_dx, cy, effective_ry > 17 ? 16 : 11, COLOR_PANEL);
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
        {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
        {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
        {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
        {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
        {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
        {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
        {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
        {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
        {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
        {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
        {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
        {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
        {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
        {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},
        {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
        {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
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
    case '-':
        return row == 3 ? 0x1F : 0;
    case '.':
        return row == 6 ? 0x04 : 0;
    case ':':
        return (row == 2 || row == 4) ? 0x04 : 0;
    case '_':
        return row == 6 ? 0x1F : 0;
    case '?': {
        static const uint8_t question[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
        return question[row];
    }
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

static void draw_mouth_to_strip(int strip_y, int strip_lines, mouth_kind_t mouth, uint16_t color)
{
    switch (mouth) {
    case MOUTH_SMILE:
        draw_line_to_strip(strip_y, strip_lines, 92, 156, 108, 170, 2, color);
        draw_line_to_strip(strip_y, strip_lines, 108, 170, 132, 170, 2, color);
        draw_line_to_strip(strip_y, strip_lines, 132, 170, 148, 156, 2, color);
        break;
    case MOUTH_OPEN:
        draw_disc_to_strip(strip_y, strip_lines, 120, 164, 18, color);
        draw_disc_to_strip(strip_y, strip_lines, 120, 164, 10, COLOR_BG);
        break;
    case MOUTH_FROWN:
        draw_line_to_strip(strip_y, strip_lines, 92, 170, 108, 156, 2, color);
        draw_line_to_strip(strip_y, strip_lines, 108, 156, 132, 156, 2, color);
        draw_line_to_strip(strip_y, strip_lines, 132, 156, 148, 170, 2, color);
        break;
    case MOUTH_SMALL:
        draw_line_to_strip(strip_y, strip_lines, 110, 166, 130, 166, 1, color);
        break;
    case MOUTH_LINE:
    default:
        draw_line_to_strip(strip_y, strip_lines, 94, 160, 146, 160, 2, color);
        break;
    }
}

static face_style_t face_style_for_scene(lcd_face_scene_t scene)
{
    face_style_t style = {
        .left_open = 66,
        .right_open = 66,
        .left_pupil_dx = 0,
        .right_pupil_dx = 0,
        .left_lashes = true,
        .right_lashes = true,
        .show_brows = false,
        .brow_lx0 = 54,
        .brow_ly0 = 58,
        .brow_lx1 = 86,
        .brow_ly1 = 54,
        .brow_rx0 = 154,
        .brow_ry0 = 54,
        .brow_rx1 = 186,
        .brow_ry1 = 58,
        .mouth = MOUTH_LINE,
        .iris_color = COLOR_BLUE,
        .brow_color = COLOR_DIM,
        .mouth_color = COLOR_WHITE,
        .line1 = "IDLE",
        .line2 = "WAITING",
    };

    switch (scene) {
    case LCD_FACE_SCENE_BOOT:
        style.left_open = 42;
        style.right_open = 42;
        style.left_lashes = false;
        style.right_lashes = false;
        style.mouth = MOUTH_SMALL;
        style.iris_color = COLOR_DIM;
        style.brow_color = COLOR_DIM;
        style.mouth_color = COLOR_DIM;
        style.line1 = "BOOT";
        style.line2 = "LCD READY";
        break;
    case LCD_FACE_SCENE_IDLE:
        style.left_open = 66;
        style.right_open = 66;
        style.show_brows = true;
        style.brow_ly0 = 58;
        style.brow_ly1 = 54;
        style.brow_ry0 = 54;
        style.brow_ry1 = 58;
        style.iris_color = COLOR_BLUE;
        style.mouth = MOUTH_LINE;
        style.line1 = "IDLE";
        style.line2 = "WAITING";
        break;
    case LCD_FACE_SCENE_WINK:
        style.left_open = 6;
        style.right_open = 96;
        style.left_pupil_dx = 0;
        style.right_pupil_dx = 4;
        style.left_lashes = false;
        style.right_lashes = true;
        style.show_brows = true;
        style.brow_ly0 = 54;
        style.brow_ly1 = 48;
        style.brow_ry0 = 50;
        style.brow_ry1 = 54;
        style.iris_color = COLOR_GOLD;
        style.brow_color = COLOR_GOLD;
        style.mouth = MOUTH_SMILE;
        style.mouth_color = COLOR_GOLD;
        style.line1 = "WINK";
        style.line2 = "PLAY MODE";
        break;
    case LCD_FACE_SCENE_HAPPY:
        style.left_open = 92;
        style.right_open = 92;
        style.left_pupil_dx = -2;
        style.right_pupil_dx = 2;
        style.show_brows = true;
        style.brow_ly0 = 52;
        style.brow_ly1 = 48;
        style.brow_ry0 = 48;
        style.brow_ry1 = 52;
        style.iris_color = COLOR_GREEN;
        style.brow_color = COLOR_GREEN;
        style.mouth = MOUTH_SMILE;
        style.mouth_color = COLOR_GREEN;
        style.line1 = "HAPPY";
        style.line2 = "SMILE";
        break;
    case LCD_FACE_SCENE_SURPRISED:
        style.left_open = 100;
        style.right_open = 100;
        style.left_pupil_dx = 0;
        style.right_pupil_dx = 0;
        style.show_brows = true;
        style.brow_ly0 = 48;
        style.brow_ly1 = 44;
        style.brow_ry0 = 44;
        style.brow_ry1 = 48;
        style.iris_color = COLOR_CYAN;
        style.brow_color = COLOR_CYAN;
        style.mouth = MOUTH_OPEN;
        style.mouth_color = COLOR_ORANGE;
        style.line1 = "WOW";
        style.line2 = "SURPRISED";
        break;
    case LCD_FACE_SCENE_SLEEPY:
        style.left_open = 18;
        style.right_open = 22;
        style.left_lashes = false;
        style.right_lashes = false;
        style.show_brows = true;
        style.brow_ly0 = 60;
        style.brow_ly1 = 66;
        style.brow_ry0 = 66;
        style.brow_ry1 = 60;
        style.iris_color = COLOR_DIM;
        style.brow_color = COLOR_DIM;
        style.mouth = MOUTH_SMALL;
        style.mouth_color = COLOR_DIM;
        style.line1 = "SLEEPY";
        style.line2 = "REST TIME";
        break;
    case LCD_FACE_SCENE_ANGRY:
    default:
        style.left_open = 48;
        style.right_open = 48;
        style.left_pupil_dx = 0;
        style.right_pupil_dx = 0;
        style.show_brows = true;
        style.brow_lx0 = 54;
        style.brow_ly0 = 50;
        style.brow_lx1 = 86;
        style.brow_ly1 = 58;
        style.brow_rx0 = 154;
        style.brow_ry0 = 58;
        style.brow_rx1 = 186;
        style.brow_ry1 = 50;
        style.iris_color = COLOR_RED;
        style.brow_color = COLOR_RED;
        style.mouth = MOUTH_FROWN;
        style.mouth_color = COLOR_RED;
        style.line1 = "ANGRY";
        style.line2 = "FOCUS UP";
        break;
    }

    return style;
}

static void draw_face_to_strip(int strip_y, int strip_lines, lcd_face_scene_t scene, const char *line1, const char *line2)
{
    const face_style_t style = face_style_for_scene(scene);
    const char *caption = (line1 && line1[0]) ? line1 : style.line1;
    const char *subcaption = (line2 && line2[0]) ? line2 : style.line2;

    fill_strip(strip_lines, COLOR_BG);

    draw_disc_to_strip(strip_y, strip_lines, 60, 74, 32, COLOR_SHADOW);
    draw_disc_to_strip(strip_y, strip_lines, 180, 74, 32, COLOR_SHADOW);
    draw_eye_to_strip(strip_y, strip_lines, 60, 86, 42, 28, style.left_pupil_dx, style.left_open, style.left_lashes, style.iris_color);
    draw_eye_to_strip(strip_y, strip_lines, 180, 86, 42, 28, style.right_pupil_dx, style.right_open, style.right_lashes, style.iris_color);

    if (style.show_brows) {
        draw_line_to_strip(strip_y, strip_lines, style.brow_lx0, style.brow_ly0, style.brow_lx1, style.brow_ly1, 2, style.brow_color);
        draw_line_to_strip(strip_y, strip_lines, style.brow_rx0, style.brow_ry0, style.brow_rx1, style.brow_ry1, 2, style.brow_color);
    }

    draw_mouth_to_strip(strip_y, strip_lines, style.mouth, style.mouth_color);

    draw_centered_text_to_strip(strip_y, strip_lines, caption, 198, 2, COLOR_WHITE);
    if (subcaption && subcaption[0]) {
        draw_centered_text_to_strip(strip_y, strip_lines, subcaption, 220, 1, COLOR_DIM);
    }
}

static void lcd_render_scene(lcd_face_scene_t scene, const char *line1, const char *line2)
{
    if (!s_panel || !s_lcd_mutex) {
        return;
    }

    xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);

    for (int y = 0; y < LCD_V_RES; y += LCD_FLUSH_LINES) {
        const int lines = (LCD_V_RES - y) < LCD_FLUSH_LINES ? (LCD_V_RES - y) : LCD_FLUSH_LINES;
        draw_face_to_strip(y, lines, scene, line1, line2);

        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + lines, s_line_buffer));
        if (!lcd_wait_flush_done()) {
            break;
        }
    }

    xSemaphoreGive(s_lcd_mutex);
}

esp_err_t lcd_face_ui_init(void)
{
    if (s_panel) {
        return ESP_OK;
    }

    s_flush_done_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_flush_done_sem, ESP_ERR_NO_MEM, TAG, "create flush semaphore failed");

    s_lcd_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lcd_mutex, ESP_ERR_NO_MEM, TAG, "create LCD mutex failed");

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

    lcd_render_scene(LCD_FACE_SCENE_BOOT, "BOOT", "LCD FACE CYCLE");
    return ESP_OK;
}

void lcd_face_ui_show_scene(lcd_face_scene_t scene, const char *line1, const char *line2)
{
    lcd_render_scene(scene, line1, line2);
}
