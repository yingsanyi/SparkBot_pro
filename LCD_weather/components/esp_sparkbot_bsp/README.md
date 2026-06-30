# ESP-SparkBot BSP 天气显示子集

这个本地 BSP 副本只服务于当前 LCD 天气显示工程。

保留内容：
- ST7789 LCD 初始化与背光控制
- 天气界面使用的触摸/按键
- I2C 辅助函数
- ADC 电量读取

已取消：
- 麦克风
- I2S
- ES8311/esp_codec_dev 音频链路
- 语音识别相关能力
