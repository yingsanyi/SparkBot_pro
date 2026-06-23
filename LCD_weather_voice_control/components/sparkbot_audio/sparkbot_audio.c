#include <stdbool.h>
#include <math.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "sparkbot_audio.h"

static const char *TAG = "sparkbot_audio";

#define SPARKBOT_I2S_GPIO_CFG       \
    {                               \
        .mclk = SPARKBOT_I2S_MCLK,  \
        .bclk = SPARKBOT_I2S_SCLK,  \
        .ws = SPARKBOT_I2S_LRCK,    \
        .dout = SPARKBOT_I2S_DOUT,  \
        .din = SPARKBOT_I2S_DSIN,   \
        .invert_flags = {           \
            .mclk_inv = false,      \
            .bclk_inv = false,      \
            .ws_inv = false,        \
        },                          \
    }

#define SPARKBOT_I2S_DUPLEX_MONO_CFG(sample_rate)                                                    \
    {                                                                                                 \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),                                           \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), \
        .gpio_cfg = SPARKBOT_I2S_GPIO_CFG,                                                            \
    }

static const audio_codec_data_if_t *s_i2s_data_if = NULL;
static i2s_chan_handle_t s_i2s_tx_chan = NULL;
static i2s_chan_handle_t s_i2s_rx_chan = NULL;
static bool s_i2c_initialized = false;
static esp_codec_dev_handle_t s_speaker = NULL;
static bool s_speaker_opened = false;

esp_err_t sparkbot_audio_power_amp_set(bool enable)
{
    gpio_num_t amp_io = SPARKBOT_POWER_AMP_IO;
    if (amp_io == GPIO_NUM_NC) {
        return ESP_OK;
    }

    const gpio_config_t amp_config = {
        .pin_bit_mask = 1ULL << (uint32_t)amp_io,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&amp_config), TAG, "power amplifier GPIO config failed");
    return gpio_set_level(amp_io, enable ? 1 : 0);
}

static esp_err_t sparkbot_audio_i2c_init(void)
{
    if (s_i2c_initialized) {
        return ESP_OK;
    }

    const i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SPARKBOT_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = SPARKBOT_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(SPARKBOT_I2C_NUM, &i2c_config), TAG, "I2C parameter config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(SPARKBOT_I2C_NUM, i2c_config.mode, 0, 0, 0), TAG, "I2C driver install failed");

    s_i2c_initialized = true;
    return ESP_OK;
}

esp_err_t sparkbot_audio_init(const i2s_std_config_t *i2s_config)
{
    if (s_i2s_tx_chan && s_i2s_rx_chan) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_SPARKBOT_AUDIO_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx_chan, &s_i2s_rx_chan), TAG, "create I2S channels failed");

    const i2s_std_config_t default_i2s_cfg = SPARKBOT_I2S_DUPLEX_MONO_CFG(16000);
    const i2s_std_config_t *active_i2s_cfg = i2s_config ? i2s_config : &default_i2s_cfg;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx_chan, active_i2s_cfg), TAG, "init I2S TX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_rx_chan, active_i2s_cfg), TAG, "init I2S RX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx_chan), TAG, "enable I2S TX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx_chan), TAG, "enable I2S RX failed");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = CONFIG_SPARKBOT_AUDIO_I2S_NUM,
        .tx_handle = s_i2s_tx_chan,
        .rx_handle = s_i2s_rx_chan,
    };
    s_i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(s_i2s_data_if, ESP_ERR_NO_MEM, TAG, "create codec I2S data interface failed");

    return ESP_OK;
}

esp_codec_dev_handle_t sparkbot_audio_codec_microphone_init(void)
{
    ESP_ERROR_CHECK(sparkbot_audio_i2c_init());
    ESP_ERROR_CHECK(sparkbot_audio_init(NULL));
    ESP_ERROR_CHECK(sparkbot_audio_power_amp_set(false));

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_ERROR_CHECK(gpio_if ? ESP_OK : ESP_ERR_NO_MEM);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = SPARKBOT_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_ERROR_CHECK(i2c_ctrl_if ? ESP_OK : ESP_ERR_NO_MEM);

    const esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = SPARKBOT_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    ESP_ERROR_CHECK(es8311_dev ? ESP_OK : ESP_ERR_NO_MEM);

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es8311_dev,
        .data_if = s_i2s_data_if,
    };

    esp_codec_dev_handle_t codec = esp_codec_dev_new(&codec_dev_cfg);
    ESP_ERROR_CHECK(codec ? ESP_OK : ESP_ERR_NO_MEM);
    return codec;
}

esp_codec_dev_handle_t sparkbot_audio_codec_speaker_init(void)
{
    if (s_speaker) {
        return s_speaker;
    }

    ESP_ERROR_CHECK(sparkbot_audio_i2c_init());
    ESP_ERROR_CHECK(sparkbot_audio_init(NULL));

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_ERROR_CHECK(gpio_if ? ESP_OK : ESP_ERR_NO_MEM);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = SPARKBOT_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_ERROR_CHECK(i2c_ctrl_if ? ESP_OK : ESP_ERR_NO_MEM);

    const esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_TYPE_OUT,
        .pa_pin = SPARKBOT_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    ESP_ERROR_CHECK(es8311_dev ? ESP_OK : ESP_ERR_NO_MEM);

    esp_codec_dev_cfg_t codec_dev_cfg = {
        /* Keep RX enabled while playing prompts so command audio still reaches AFE. */
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = es8311_dev,
        .data_if = s_i2s_data_if,
    };

    s_speaker = esp_codec_dev_new(&codec_dev_cfg);
    ESP_ERROR_CHECK(s_speaker ? ESP_OK : ESP_ERR_NO_MEM);
    return s_speaker;
}

esp_err_t sparkbot_audio_prepare_speaker(int volume)
{
    esp_codec_dev_handle_t speaker = sparkbot_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(speaker, ESP_ERR_INVALID_STATE, TAG, "speaker init failed");

    if (!s_speaker_opened) {
        esp_codec_dev_sample_info_t fs = {
            .sample_rate = 16000,
            .channel = 1,
            .bits_per_sample = 16,
        };

        ESP_RETURN_ON_ERROR(esp_codec_dev_open(speaker, &fs), TAG, "open speaker failed");
        s_speaker_opened = true;
    }

    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(speaker, volume), TAG, "set volume failed");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_mute(speaker, false), TAG, "unmute speaker failed");
    ESP_RETURN_ON_ERROR(sparkbot_audio_power_amp_set(true), TAG, "enable amp failed");
    return ESP_OK;
}

esp_err_t sparkbot_audio_play_beep(uint32_t frequency_hz, uint32_t duration_ms, int volume)
{
    esp_codec_dev_handle_t speaker = sparkbot_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(speaker, ESP_ERR_INVALID_STATE, TAG, "speaker init failed");

    const uint32_t sample_rate = 16000;
    const uint32_t samples = (sample_rate * duration_ms) / 1000;
    const uint32_t chunk_samples = 160;
    int16_t buffer[chunk_samples];

    ESP_RETURN_ON_ERROR(sparkbot_audio_prepare_speaker(volume), TAG, "prepare speaker failed");

    uint32_t produced = 0;
    while (produced < samples) {
        const uint32_t now = (samples - produced) < chunk_samples ? (samples - produced) : chunk_samples;
        for (uint32_t i = 0; i < now; i++) {
            const float t = (float)(produced + i) / (float)sample_rate;
            const float envelope = (i < 16) ? (float)i / 16.0f : 1.0f;
            buffer[i] = (int16_t)(sinf(2.0f * 3.1415926f * (float)frequency_hz * t) * envelope * 8000.0f);
        }
        ESP_RETURN_ON_ERROR(esp_codec_dev_write(speaker, buffer, now * sizeof(buffer[0])), TAG, "write beep failed");
        produced += now;
    }

    return ESP_OK;
}
