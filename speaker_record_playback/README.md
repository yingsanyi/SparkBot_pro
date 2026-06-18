# Speaker Record Playback

这个项目基于 `esp_sparkbot-master/example/factory_demo_v1` 的 ES8311 音频方案，做一个独立的 ESP-IDF 示例：

1. 喇叭播放“开始录音”
2. 麦克风录制人的声音
3. 喇叭播放“录音结束”
4. 喇叭回放刚刚录到的声音

项目不直接依赖 `E:\practiceWeek\codes\esp_sparkbot-master` 源码，已经把需要的音频初始化逻辑整理到本项目的 `components/sparkbot_audio` 中。

## 硬件连接

参考原理图和官方示例，板载 ES8311 codec 使用 I2C 配置，使用 I2S 同时传输播放和录音数据。

| 功能 | ESP32-S3 引脚 | 说明 |
| --- | --- | --- |
| I2C_SCL | GPIO5 | 配置 ES8311 |
| I2C_SDA | GPIO4 | 配置 ES8311 |
| I2S_MCLK | GPIO45 | ES8311 主时钟 |
| I2S_SCLK | GPIO39 | I2S 位时钟 |
| I2S_LRCK | GPIO41 | I2S 左右声道时钟 |
| I2S_DOUT | GPIO42 | ESP32-S3 输出到 ES8311，用于喇叭播放 |
| I2S_DSIN | GPIO40 | ES8311 输入到 ESP32-S3，用于麦克风录音 |
| PA_CTRL | GPIO46 | 功放使能 |

麦克风模块原理图中的 `MICP_SUB`、`MICN_SUB`、`ADCVREF_SUB` 是模拟麦克风信号，进入 ES8311；程序侧不需要额外 GPIO 读取麦克风，而是通过 ES8311 的 ADC 和 I2S RX 获取 PCM 数据。

## 关键代码

音频引脚在 [components/sparkbot_audio/include/sparkbot_audio.h](components/sparkbot_audio/include/sparkbot_audio.h) 中定义：

```c
#define SPARKBOT_I2S_MCLK       (GPIO_NUM_45)
#define SPARKBOT_I2S_SCLK       (GPIO_NUM_39)
#define SPARKBOT_I2S_DOUT       (GPIO_NUM_42)
#define SPARKBOT_I2S_LRCK       (GPIO_NUM_41)
#define SPARKBOT_I2S_DSIN       (GPIO_NUM_40)
#define SPARKBOT_I2C_SCL        (GPIO_NUM_5)
#define SPARKBOT_I2C_SDA        (GPIO_NUM_4)
#define SPARKBOT_POWER_AMP_IO   (GPIO_NUM_46)
```

I2S 在 [components/sparkbot_audio/sparkbot_audio.c](components/sparkbot_audio/sparkbot_audio.c) 中创建为双向通道：

```c
i2s_new_channel(&chan_cfg, &s_i2s_tx_chan, &s_i2s_rx_chan);
i2s_channel_init_std_mode(s_i2s_tx_chan, active_i2s_cfg);
i2s_channel_init_std_mode(s_i2s_rx_chan, active_i2s_cfg);
```

录音和回放主流程在 [main/speaker_record_playback_main.c](main/speaker_record_playback_main.c)：

```c
play_prompt(speaker, &start_prompt, "start recording");
record_voice(speaker, microphone, record_buffer, record_bytes);
play_prompt(speaker, &end_prompt, "recording finished");
write_pcm_chunks(speaker, record_buffer, record_bytes);
```

录音期间程序会静音并关闭功放：

```c
esp_codec_dev_set_out_mute(speaker, true);
sparkbot_audio_power_amp_set(false);
```

这样可以减少喇叭声音被麦克风重新录进去的问题。

## 参数

默认参数在 `sdkconfig.defaults`：

```ini
CONFIG_RECORD_PLAYBACK_VOLUME=80
CONFIG_RECORD_PLAYBACK_SECONDS=5
CONFIG_RECORD_PLAYBACK_MIC_GAIN_DB=35
CONFIG_SPARKBOT_AUDIO_I2C_NUM=0
CONFIG_SPARKBOT_AUDIO_I2S_NUM=0
```

常用修改：

- 录音太短：增大 `CONFIG_RECORD_PLAYBACK_SECONDS`
- 回放太小：增大 `CONFIG_RECORD_PLAYBACK_VOLUME`
- 麦克风声音太小：增大 `CONFIG_RECORD_PLAYBACK_MIC_GAIN_DB`
- 麦克风爆音或失真：减小 `CONFIG_RECORD_PLAYBACK_MIC_GAIN_DB`

## 编译和烧录

在项目目录执行：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

运行后，串口会看到：

```text
Speaker record playback demo started
Play prompt: start recording
Recording 5 second(s)
Recording finished
Play prompt: recording finished
Play back recorded voice
Demo finished
```

如果串口正常但喇叭没有声音，优先检查：

- 是否烧录的是 `speaker_record_playback` 项目
- 功放控制是否为 GPIO46
- 喇叭是否接在 `PA_OUT+` / `PA_OUT-`
- 音量是否太低

如果提示音能播放但回放没有声音，优先检查：

- 麦克风模块是否连接到 `MICP_SUB` / `MICN_SUB` / `ADCVREF_SUB`
- `I2S_DSIN` 是否为 GPIO40
- 麦克风增益是否太低
