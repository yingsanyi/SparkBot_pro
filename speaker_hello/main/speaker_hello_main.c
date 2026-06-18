/*
 * Minimal ESP-SparkBot speaker test.
 *
 * The firmware initializes the ES8311 speaker path and plays an embedded WAV
 * that says "你好".
 */

#include <stdint.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sparkbot_audio.h"

static const char *TAG = "speaker_hello";

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This speaker example is for the ESP-SparkBot ESP32-S3 hardware."
#endif

#ifndef CONFIG_SPEAKER_HELLO_VOLUME
#define CONFIG_SPEAKER_HELLO_VOLUME 75
#endif

#ifndef CONFIG_SPEAKER_HELLO_REPEAT_DELAY_MS
#define CONFIG_SPEAKER_HELLO_REPEAT_DELAY_MS 3000
#endif

extern const uint8_t nihao_wav_start[] asm("_binary_nihao_wav_start");
extern const uint8_t nihao_wav_end[] asm("_binary_nihao_wav_end");

typedef struct {
    const uint8_t *pcm;
    uint32_t pcm_size;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} wav_info_t;

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

static void open_speaker(esp_codec_dev_handle_t speaker, const wav_info_t *wav)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = wav->sample_rate,
        .channel = wav->channels,
        .bits_per_sample = wav->bits_per_sample,
    };

    ESP_LOGI(TAG, "Open speaker: %lu Hz, %u channel, %u bit, %lu bytes",
             wav->sample_rate, wav->channels, wav->bits_per_sample, wav->pcm_size);

    ESP_ERROR_CHECK(esp_codec_dev_open(speaker, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(speaker, CONFIG_SPEAKER_HELLO_VOLUME));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_mute(speaker, false));
    ESP_ERROR_CHECK(sparkbot_audio_power_amp_set(true));
}

static void play_wav(esp_codec_dev_handle_t speaker, const wav_info_t *wav)
{
    ESP_ERROR_CHECK(esp_codec_dev_write(speaker, (void *)wav->pcm, wav->pcm_size));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Speaker hello demo started");

    wav_info_t nihao = {0};
    ESP_ERROR_CHECK(parse_wav(nihao_wav_start, nihao_wav_end - nihao_wav_start, &nihao));

    esp_codec_dev_handle_t speaker = sparkbot_audio_codec_speaker_init();
    ESP_ERROR_CHECK(speaker ? ESP_OK : ESP_ERR_NO_MEM);
    open_speaker(speaker, &nihao);

    while (1) {
        ESP_LOGI(TAG, "Play embedded audio: ni hao");
        play_wav(speaker, &nihao);

        if (CONFIG_SPEAKER_HELLO_REPEAT_DELAY_MS == 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SPEAKER_HELLO_REPEAT_DELAY_MS));
    }
}
