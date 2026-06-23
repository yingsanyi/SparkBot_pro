#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPARKBOT_I2S_MCLK       (GPIO_NUM_45)
#define SPARKBOT_I2S_SCLK       (GPIO_NUM_39)
#define SPARKBOT_I2S_DOUT       (GPIO_NUM_42)
#define SPARKBOT_I2S_LRCK       (GPIO_NUM_41)
#define SPARKBOT_I2S_DSIN       (GPIO_NUM_40)
#define SPARKBOT_I2C_SCL        (GPIO_NUM_5)
#define SPARKBOT_I2C_SDA        (GPIO_NUM_4)
#define SPARKBOT_I2C_NUM        (CONFIG_SPARKBOT_AUDIO_I2C_NUM)
#define SPARKBOT_POWER_AMP_IO   (GPIO_NUM_NC)

esp_err_t sparkbot_audio_init(const i2s_std_config_t *i2s_config);
esp_codec_dev_handle_t sparkbot_audio_codec_microphone_init(void);
esp_codec_dev_handle_t sparkbot_audio_codec_speaker_init(void);
esp_err_t sparkbot_audio_prepare_speaker(int volume);
esp_err_t sparkbot_audio_power_amp_set(bool enable);
esp_err_t sparkbot_audio_play_beep(uint32_t frequency_hz, uint32_t duration_ms, int volume);

#ifdef __cplusplus
}
#endif
