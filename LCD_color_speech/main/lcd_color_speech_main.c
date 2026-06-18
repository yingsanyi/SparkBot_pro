/*
 * ESP-SparkBot LCD color-name speech demo.
 *
 * The screen cycles through solid colors, draws the matching Chinese color
 * name, and plays an embedded WAV that speaks that color name.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_types.h"
#include "sparkbot_audio.h"

static const char *TAG = "lcd_color_speech";

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This example is for the ESP-SparkBot ESP32-S3 head hardware."
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

#ifndef CONFIG_LCD_COLOR_SPEECH_HOLD_MS
#define CONFIG_LCD_COLOR_SPEECH_HOLD_MS 2500
#endif

#ifndef CONFIG_LCD_COLOR_SPEECH_VOLUME
#define CONFIG_LCD_COLOR_SPEECH_VOLUME 75
#endif

#define LCD_RGB565_BLACK               0x0000
#define LCD_RGB565_WHITE               0xFFFF

#define COLOR_NAME_GLYPH_SIZE          40
#define COLOR_NAME_GLYPH_GAP           8
#define COLOR_NAME_TEXT_WIDTH          (COLOR_NAME_GLYPH_SIZE * 2 + COLOR_NAME_GLYPH_GAP)
#define COLOR_NAME_TEXT_X              ((LCD_H_RES - COLOR_NAME_TEXT_WIDTH) / 2)
#define COLOR_NAME_TEXT_Y              ((LCD_V_RES - COLOR_NAME_GLYPH_SIZE) / 2)

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

typedef enum {
    COLOR_GLYPH_RED = 0,
    COLOR_GLYPH_GREEN,
    COLOR_GLYPH_BLUE,
    COLOR_GLYPH_YELLOW,
    COLOR_GLYPH_PURPLE,
    COLOR_GLYPH_COLOR,
    COLOR_NAME_GLYPH_COUNT,
} color_name_glyph_t;

#include "color_name_glyphs.inc"

extern const uint8_t red_wav_start[] asm("_binary_red_wav_start");
extern const uint8_t red_wav_end[] asm("_binary_red_wav_end");
extern const uint8_t green_wav_start[] asm("_binary_green_wav_start");
extern const uint8_t green_wav_end[] asm("_binary_green_wav_end");
extern const uint8_t blue_wav_start[] asm("_binary_blue_wav_start");
extern const uint8_t blue_wav_end[] asm("_binary_blue_wav_end");
extern const uint8_t yellow_wav_start[] asm("_binary_yellow_wav_start");
extern const uint8_t yellow_wav_end[] asm("_binary_yellow_wav_end");
extern const uint8_t purple_wav_start[] asm("_binary_purple_wav_start");
extern const uint8_t purple_wav_end[] asm("_binary_purple_wav_end");

typedef struct {
    const uint8_t *pcm;
    uint32_t pcm_size;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} wav_info_t;

typedef struct {
    const char *name;
    uint16_t background;
    uint16_t foreground;
    color_name_glyph_t first_glyph;
    const uint8_t *wav_start;
    const uint8_t *wav_end;
} color_speech_mode_t;

static const color_speech_mode_t s_modes[] = {
    { "red", RGB565(255, 0, 0), LCD_RGB565_WHITE, COLOR_GLYPH_RED, red_wav_start, red_wav_end },
    { "green", RGB565(0, 255, 0), LCD_RGB565_BLACK, COLOR_GLYPH_GREEN, green_wav_start, green_wav_end },
    { "blue", RGB565(0, 64, 255), LCD_RGB565_WHITE, COLOR_GLYPH_BLUE, blue_wav_start, blue_wav_end },
    { "yellow", RGB565(255, 210, 0), LCD_RGB565_BLACK, COLOR_GLYPH_YELLOW, yellow_wav_start, yellow_wav_end },
    { "purple", RGB565(160, 32, 240), LCD_RGB565_WHITE, COLOR_GLYPH_PURPLE, purple_wav_start, purple_wav_end },
};

#define MODE_COUNT ((int)(sizeof(s_modes) / sizeof(s_modes[0])))

static esp_lcd_panel_handle_t s_panel = NULL;
static SemaphoreHandle_t s_flush_done_sem = NULL;
static DMA_ATTR uint16_t s_line_buffer[LCD_H_RES * LCD_FLUSH_LINES];
static esp_codec_dev_handle_t s_speaker = NULL;
static wav_info_t s_wavs[MODE_COUNT];

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

static void draw_color_name_glyph_to_strip(int strip_y,
                                           int strip_lines,
                                           color_name_glyph_t glyph,
                                           int x,
                                           int y,
                                           uint16_t color)
{
    if (glyph < 0 || glyph >= COLOR_NAME_GLYPH_COUNT) {
        return;
    }

    for (int row = 0; row < COLOR_NAME_GLYPH_SIZE; row++) {
        const int screen_y = y + row;
        if (screen_y < strip_y || screen_y >= strip_y + strip_lines) {
            continue;
        }
        if (screen_y < 0 || screen_y >= LCD_V_RES) {
            continue;
        }

        uint16_t *line = &s_line_buffer[(screen_y - strip_y) * LCD_H_RES];
        const uint64_t bits = s_color_name_glyph_rows[glyph][row];
        for (int col = 0; col < COLOR_NAME_GLYPH_SIZE; col++) {
            const int screen_x = x + col;
            if (screen_x < 0 || screen_x >= LCD_H_RES) {
                continue;
            }
            if (bits & (1ULL << (COLOR_NAME_GLYPH_SIZE - 1 - col))) {
                line[screen_x] = color;
            }
        }
    }
}

static void lcd_draw_color_name_screen(const color_speech_mode_t *mode)
{
    const int second_glyph_x = COLOR_NAME_TEXT_X + COLOR_NAME_GLYPH_SIZE + COLOR_NAME_GLYPH_GAP;

    for (int y = 0; y < LCD_V_RES; y += LCD_FLUSH_LINES) {
        const int lines = (LCD_V_RES - y) < LCD_FLUSH_LINES ? (LCD_V_RES - y) : LCD_FLUSH_LINES;
        fill_strip(lines, mode->background);
        draw_color_name_glyph_to_strip(y, lines, mode->first_glyph, COLOR_NAME_TEXT_X, COLOR_NAME_TEXT_Y, mode->foreground);
        draw_color_name_glyph_to_strip(y, lines, COLOR_GLYPH_COLOR, second_glyph_x, COLOR_NAME_TEXT_Y, mode->foreground);

        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + lines, s_line_buffer));
        lcd_wait_flush_done();
    }
}

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static esp_err_t parse_wav(const uint8_t *data, uint32_t size, wav_info_t *out_info)
{
    if (size < 44 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAV header");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t offset = 12;
    bool found_fmt = false;
    bool found_data = false;
    wav_info_t info = {0};

    while (offset + 8 <= size) {
        const uint8_t *chunk = data + offset;
        const uint32_t chunk_size = read_le32(chunk + 4);
        const uint32_t payload = offset + 8;

        if (payload + chunk_size > size) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                return ESP_ERR_INVALID_SIZE;
            }

            const uint16_t audio_format = read_le16(data + payload);
            info.channels = read_le16(data + payload + 2);
            info.sample_rate = read_le32(data + payload + 4);
            info.bits_per_sample = read_le16(data + payload + 14);

            if (audio_format != 1) {
                ESP_LOGE(TAG, "Only PCM WAV is supported, format=%u", audio_format);
                return ESP_ERR_NOT_SUPPORTED;
            }
            found_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            info.pcm = data + payload;
            info.pcm_size = chunk_size;
            found_data = true;
        }

        offset = payload + chunk_size + (chunk_size & 1U);
    }

    if (!found_fmt || !found_data || !info.pcm || info.pcm_size == 0) {
        ESP_LOGE(TAG, "WAV fmt/data chunk missing");
        return ESP_ERR_NOT_FOUND;
    }

    *out_info = info;
    return ESP_OK;
}

static esp_err_t check_wav_matches_first(const wav_info_t *first, const wav_info_t *current)
{
    if (current->sample_rate != first->sample_rate ||
        current->channels != first->channels ||
        current->bits_per_sample != first->bits_per_sample) {
        ESP_LOGE(TAG, "All embedded WAV files must use the same sample format");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static void audio_init(void)
{
    for (int i = 0; i < MODE_COUNT; i++) {
        ESP_ERROR_CHECK(parse_wav(s_modes[i].wav_start,
                                  (uint32_t)(s_modes[i].wav_end - s_modes[i].wav_start),
                                  &s_wavs[i]));
        if (i > 0) {
            ESP_ERROR_CHECK(check_wav_matches_first(&s_wavs[0], &s_wavs[i]));
        }
    }

    ESP_LOGI(TAG, "Initialize ES8311 speaker path");
    s_speaker = sparkbot_audio_codec_speaker_init();
    ESP_ERROR_CHECK(s_speaker ? ESP_OK : ESP_ERR_NO_MEM);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = s_wavs[0].sample_rate,
        .channel = s_wavs[0].channels,
        .bits_per_sample = s_wavs[0].bits_per_sample,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(s_speaker, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(s_speaker, CONFIG_LCD_COLOR_SPEECH_VOLUME));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_mute(s_speaker, false));
    ESP_ERROR_CHECK(sparkbot_audio_power_amp_set(true));
}

static void audio_play_wav(const wav_info_t *wav)
{
    ESP_ERROR_CHECK(sparkbot_audio_power_amp_set(true));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_mute(s_speaker, false));
    ESP_ERROR_CHECK(esp_codec_dev_write(s_speaker, (void *)wav->pcm, wav->pcm_size));
}

static void run_mode(int mode_index)
{
    const color_speech_mode_t *mode = &s_modes[mode_index];

    ESP_LOGI(TAG, "mode=%s bg=0x%04x", mode->name, mode->background);
    lcd_backlight_set(true);
    lcd_draw_color_name_screen(mode);
    audio_play_wav(&s_wavs[mode_index]);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_LCD_COLOR_SPEECH_HOLD_MS));
}

void app_main(void)
{
    ESP_LOGI(TAG, "LCD color speech demo started");
    lcd_init();
    audio_init();

    while (1) {
        for (int i = 0; i < MODE_COUNT; i++) {
            run_mode(i);
        }
    }
}
