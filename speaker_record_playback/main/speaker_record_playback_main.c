/*
 * ESP-SparkBot speaker and microphone example.
 *
 * Flow:
 * 1. play "start recording" prompt
 * 2. record local microphone PCM
 * 3. play "recording finished" prompt
 * 4. play back the recorded voice
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sparkbot_audio.h"

static const char *TAG = "record_playback";

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This example is for the ESP-SparkBot ESP32-S3 hardware."
#endif

#ifndef CONFIG_RECORD_PLAYBACK_VOLUME
#define CONFIG_RECORD_PLAYBACK_VOLUME 80
#endif

#ifndef CONFIG_RECORD_PLAYBACK_SECONDS
#define CONFIG_RECORD_PLAYBACK_SECONDS 5
#endif

#ifndef CONFIG_RECORD_PLAYBACK_MIC_GAIN_DB
#define CONFIG_RECORD_PLAYBACK_MIC_GAIN_DB 35
#endif

#define AUDIO_SAMPLE_RATE_HZ 16000
#define AUDIO_CHANNELS 1
#define AUDIO_BITS_PER_SAMPLE 16
#define AUDIO_BYTES_PER_SAMPLE ((AUDIO_BITS_PER_SAMPLE / 8) * AUDIO_CHANNELS)
#define AUDIO_CHUNK_BYTES 2048

extern const uint8_t start_record_wav_start[] asm("_binary_start_record_wav_start");
extern const uint8_t start_record_wav_end[] asm("_binary_start_record_wav_end");
extern const uint8_t end_record_wav_start[] asm("_binary_end_record_wav_start");
extern const uint8_t end_record_wav_end[] asm("_binary_end_record_wav_end");

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

static esp_err_t check_wav_format(const wav_info_t *wav)
{
    if (wav->sample_rate != AUDIO_SAMPLE_RATE_HZ ||
        wav->channels != AUDIO_CHANNELS ||
        wav->bits_per_sample != AUDIO_BITS_PER_SAMPLE) {
        ESP_LOGE(TAG, "Prompt WAV must be %d Hz, %d channel, %d bit",
                 AUDIO_SAMPLE_RATE_HZ, AUDIO_CHANNELS, AUDIO_BITS_PER_SAMPLE);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t codec_result_to_err(int result)
{
    return result == 0 ? ESP_OK : (esp_err_t)result;
}

static esp_err_t open_audio_devices(esp_codec_dev_handle_t speaker, esp_codec_dev_handle_t microphone)
{
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        .channel = AUDIO_CHANNELS,
        .bits_per_sample = AUDIO_BITS_PER_SAMPLE,
    };

    ESP_RETURN_ON_ERROR(codec_result_to_err(esp_codec_dev_open(speaker, &fs)),
                        TAG, "open speaker failed");
    ESP_RETURN_ON_ERROR(codec_result_to_err(esp_codec_dev_set_out_vol(speaker, CONFIG_RECORD_PLAYBACK_VOLUME)),
                        TAG, "set speaker volume failed");
    ESP_RETURN_ON_ERROR(codec_result_to_err(esp_codec_dev_set_out_mute(speaker, false)),
                        TAG, "unmute speaker failed");

    ESP_RETURN_ON_ERROR(codec_result_to_err(esp_codec_dev_open(microphone, &fs)),
                        TAG, "open microphone failed");
    ESP_RETURN_ON_ERROR(codec_result_to_err(esp_codec_dev_set_in_gain(microphone, (float)CONFIG_RECORD_PLAYBACK_MIC_GAIN_DB)),
                        TAG, "set microphone gain failed");

    return sparkbot_audio_power_amp_set(true);
}

static esp_err_t write_pcm_chunks(esp_codec_dev_handle_t speaker, const uint8_t *pcm, size_t bytes)
{
    uint8_t chunk[AUDIO_CHUNK_BYTES];
    size_t offset = 0;

    while (offset < bytes) {
        const size_t todo = (bytes - offset) > sizeof(chunk) ? sizeof(chunk) : (bytes - offset);
        memcpy(chunk, pcm + offset, todo);
        ESP_RETURN_ON_ERROR(codec_result_to_err(esp_codec_dev_write(speaker, chunk, (int)todo)),
                            TAG, "speaker write failed");
        offset += todo;
    }

    return ESP_OK;
}

static esp_err_t play_prompt(esp_codec_dev_handle_t speaker, const wav_info_t *wav, const char *name)
{
    ESP_LOGI(TAG, "Play prompt: %s", name);
    ESP_RETURN_ON_ERROR(sparkbot_audio_power_amp_set(true), TAG, "enable power amplifier failed");
    ESP_RETURN_ON_ERROR(codec_result_to_err(esp_codec_dev_set_out_mute(speaker, false)), TAG, "unmute speaker failed");
    return write_pcm_chunks(speaker, wav->pcm, wav->pcm_size);
}

static uint8_t *allocate_record_buffer(size_t bytes)
{
    uint8_t *buffer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        buffer = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    return buffer;
}

static esp_err_t record_voice(esp_codec_dev_handle_t speaker,
                              esp_codec_dev_handle_t microphone,
                              uint8_t *buffer,
                              size_t bytes)
{
    size_t offset = 0;

    ESP_LOGI(TAG, "Recording %d second(s), %u byte(s)", CONFIG_RECORD_PLAYBACK_SECONDS, (unsigned)bytes);
    ESP_RETURN_ON_ERROR(codec_result_to_err(esp_codec_dev_set_out_mute(speaker, true)), TAG, "mute speaker failed");
    ESP_RETURN_ON_ERROR(sparkbot_audio_power_amp_set(false), TAG, "disable power amplifier failed");
    vTaskDelay(pdMS_TO_TICKS(250));

    while (offset < bytes) {
        const size_t todo = (bytes - offset) > AUDIO_CHUNK_BYTES ? AUDIO_CHUNK_BYTES : (bytes - offset);
        ESP_RETURN_ON_ERROR(codec_result_to_err(esp_codec_dev_read(microphone, buffer + offset, (int)todo)),
                            TAG, "microphone read failed");
        offset += todo;
    }

    ESP_LOGI(TAG, "Recording finished");
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Speaker record playback demo started");

    wav_info_t start_prompt = {0};
    wav_info_t end_prompt = {0};
    ESP_ERROR_CHECK(parse_wav(start_record_wav_start, start_record_wav_end - start_record_wav_start, &start_prompt));
    ESP_ERROR_CHECK(parse_wav(end_record_wav_start, end_record_wav_end - end_record_wav_start, &end_prompt));
    ESP_ERROR_CHECK(check_wav_format(&start_prompt));
    ESP_ERROR_CHECK(check_wav_format(&end_prompt));

    esp_codec_dev_handle_t speaker = sparkbot_audio_codec_speaker_init();
    esp_codec_dev_handle_t microphone = sparkbot_audio_codec_microphone_init();
    ESP_ERROR_CHECK(speaker ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(microphone ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(open_audio_devices(speaker, microphone));

    const size_t record_bytes = AUDIO_SAMPLE_RATE_HZ * AUDIO_BYTES_PER_SAMPLE * CONFIG_RECORD_PLAYBACK_SECONDS;
    uint8_t *record_buffer = allocate_record_buffer(record_bytes);
    ESP_ERROR_CHECK(record_buffer ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(play_prompt(speaker, &start_prompt, "start recording"));
    ESP_ERROR_CHECK(record_voice(speaker, microphone, record_buffer, record_bytes));
    ESP_ERROR_CHECK(play_prompt(speaker, &end_prompt, "recording finished"));

    ESP_LOGI(TAG, "Play back recorded voice");
    ESP_ERROR_CHECK(write_pcm_chunks(speaker, record_buffer, record_bytes));
    free(record_buffer);

    ESP_LOGI(TAG, "Demo finished");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
