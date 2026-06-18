#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Audio pins from the ESP-SparkBot schematic and official example project. */
#define SPARKBOT_I2S_MCLK       (GPIO_NUM_45)
#define SPARKBOT_I2S_SCLK       (GPIO_NUM_39)
#define SPARKBOT_I2S_DOUT       (GPIO_NUM_42)
#define SPARKBOT_I2S_LRCK       (GPIO_NUM_41)
#define SPARKBOT_I2S_DSIN       (GPIO_NUM_40)
#define SPARKBOT_I2C_SCL        (GPIO_NUM_5)
#define SPARKBOT_I2C_SDA        (GPIO_NUM_4)
#define SPARKBOT_I2C_NUM        (CONFIG_SPARKBOT_AUDIO_I2C_NUM)

/* PA_CTRL is tied to the LCD backlight control line on this board. */
#define SPARKBOT_POWER_AMP_IO   (GPIO_NUM_46)

esp_err_t sparkbot_audio_init(const i2s_std_config_t *i2s_config);
esp_err_t sparkbot_audio_power_amp_set(bool enable);
esp_codec_dev_handle_t sparkbot_audio_codec_speaker_init(void);
esp_codec_dev_handle_t sparkbot_audio_codec_microphone_init(void);

#ifdef __cplusplus
}
#endif
