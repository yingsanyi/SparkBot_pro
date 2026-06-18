/*
 * LCD color and speaker tone test for ESP-SparkBot.
 *
 * This project combines the LCD color examples with the ESP-SparkBot ES8311
 * speaker path. Each mode fills the ST7789 screen with a color and plays a
 * matching tone through I2S.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

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

static const char *TAG = "lcd_color_audio";

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This example is for the ESP-SparkBot ESP32-S3 head hardware."
#endif

/* ESP-SparkBot LCD pinout from LCD_blink and the official BSP. */
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
#define LCD_TRANSFER_TIMEOUT_MS   1000

#define AUDIO_SAMPLE_RATE_HZ      16000
#define AUDIO_BITS_PER_SAMPLE     16
#define AUDIO_CHANNELS            1
#define AUDIO_CHUNK_SAMPLES       256
#define AUDIO_PI                  3.14159265358979323846f
#define AUDIO_AMPLITUDE           9000

#ifndef CONFIG_LCD_AUDIO_MODE_HOLD_MS
#define CONFIG_LCD_AUDIO_MODE_HOLD_MS 2500
#endif

#ifndef CONFIG_LCD_AUDIO_TONE_MS
#define CONFIG_LCD_AUDIO_TONE_MS 700
#endif

#ifndef CONFIG_LCD_AUDIO_VOLUME
#define CONFIG_LCD_AUDIO_VOLUME 70
#endif

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

typedef struct {
    const char *name;
    uint16_t color;
    uint16_t tone_hz;
} lcd_audio_mode_t;

static const lcd_audio_mode_t s_modes[] = {
    { "red-do", RGB565(255, 0, 0), 262 },
    { "green-re", RGB565(0, 255, 0), 294 },
    { "blue-mi", RGB565(0, 64, 255), 330 },
    { "yellow-sol", RGB565(255, 210, 0), 392 },
    { "purple-la", RGB565(160, 32, 240), 440 },
};

static esp_lcd_panel_handle_t s_panel = NULL;
static SemaphoreHandle_t s_flush_done_sem = NULL;
static DMA_ATTR uint16_t s_line_buffer[LCD_H_RES * LCD_FLUSH_LINES];
static esp_codec_dev_handle_t s_speaker = NULL;

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

static void lcd_fill_color(uint16_t color)
{
    for (int i = 0; i < LCD_H_RES * LCD_FLUSH_LINES; i++) {
        s_line_buffer[i] = color;
    }

    for (int y = 0; y < LCD_V_RES; y += LCD_FLUSH_LINES) {
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

static void audio_init(void)
{
    ESP_LOGI(TAG, "Initialize ES8311 speaker path");
    s_speaker = sparkbot_audio_codec_speaker_init();
    ESP_ERROR_CHECK(s_speaker ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(s_speaker, CONFIG_LCD_AUDIO_VOLUME));

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .channel = AUDIO_CHANNELS,
        .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(s_speaker, &fs));
}

static void audio_play_tone(uint16_t frequency_hz, uint32_t duration_ms)
{
    const uint32_t total_samples = (AUDIO_SAMPLE_RATE_HZ * duration_ms) / 1000;
    const float phase_step = 2.0f * AUDIO_PI * frequency_hz / AUDIO_SAMPLE_RATE_HZ;
    float phase = 0.0f;
    int16_t samples[AUDIO_CHUNK_SAMPLES];

    for (uint32_t written_samples = 0; written_samples < total_samples;) {
        uint32_t samples_this_chunk = total_samples - written_samples;
        if (samples_this_chunk > AUDIO_CHUNK_SAMPLES) {
            samples_this_chunk = AUDIO_CHUNK_SAMPLES;
        }

        for (uint32_t i = 0; i < samples_this_chunk; i++) {
            samples[i] = (int16_t)(sinf(phase) * AUDIO_AMPLITUDE);
            phase += phase_step;
            if (phase >= 2.0f * AUDIO_PI) {
                phase -= 2.0f * AUDIO_PI;
            }
        }

        ESP_ERROR_CHECK(esp_codec_dev_write(s_speaker, samples, samples_this_chunk * sizeof(samples[0])));
        written_samples += samples_this_chunk;
    }
}

static void run_mode(const lcd_audio_mode_t *mode)
{
    ESP_LOGI(TAG, "mode=%s color=0x%04x tone=%uHz", mode->name, mode->color, mode->tone_hz);
    lcd_backlight_set(true);
    lcd_fill_color(mode->color);
    audio_play_tone(mode->tone_hz, CONFIG_LCD_AUDIO_TONE_MS);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_LCD_AUDIO_MODE_HOLD_MS));
}

void app_main(void)
{
    ESP_LOGI(TAG, "LCD color and speaker tone demo started");
    lcd_init();
    audio_init();

    while (1) {
        for (int i = 0; i < (int)(sizeof(s_modes) / sizeof(s_modes[0])); i++) {
            run_mode(&s_modes[i]);
        }
    }
}
