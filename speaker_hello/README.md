# ESP-SparkBot Speaker Hello

这是一个最小喇叭实验项目：程序启动后初始化 ES8311 音频 Codec，然后通过
I2S 播放内置的 `nihao.wav`，让喇叭说出“你好”。

本项目不做 LCD 显示，不做颜色和喇叭联动。

## 硬件引脚

| 信号 | GPIO | 作用 |
| ---- | ---- | ---- |
| I2S_MCLK | GPIO45 | I2S 主时钟 |
| I2S_SCLK | GPIO39 | I2S 位时钟 |
| I2S_LRCK | GPIO41 | I2S 左右声道时钟 |
| I2S_DOUT | GPIO42 | ESP32-S3 输出音频到 ES8311 |
| I2S_DSIN | GPIO40 | ES8311 输入到 ESP32-S3，本项目未使用 |
| I2C_SCL | GPIO5 | 配置 ES8311 |
| I2C_SDA | GPIO4 | 配置 ES8311 |

## 核心流程

1. `assets/nihao.wav` 作为二进制资源嵌入固件。
2. `sparkbot_audio_codec_speaker_init()` 初始化 I2C、I2S 和 ES8311。
3. `parse_wav()` 解析 WAV 头，找到 PCM 音频数据。
4. `esp_codec_dev_open()` 按 WAV 的采样率、声道数、位宽打开 codec。
5. `esp_codec_dev_write()` 把 PCM 数据写入喇叭播放。

## 编译

```powershell
cd E:\practiceWeek\codes\get_start\speaker_hello
idf.py set-target esp32s3
idf.py build
```

## 烧录

```powershell
idf.py -p COMx flash monitor
```

把 `COMx` 换成实际串口号。

## 配置项

可以在 `idf.py menuconfig` 中调整：

```text
Speaker Hello Configuration
  Speaker volume
  Repeat delay in ms
  Audio I2C port
  Audio I2S port
```

`Repeat delay in ms` 默认是 3000，表示每隔 3 秒重复播放一次“你好”。
如果设置成 0，则只播放一次。
