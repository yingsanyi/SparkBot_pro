# SparkBot LCD 天气站

本项目的 IDF 工程名为 `sparkbot_lcd_weather`，只保留 SparkBot 的 LCD 天气显示功能。语音唤醒、语音识别、ASR、TTS、LLM 聊天和音频链路已经取消。

## 功能

- 240x240 ST7789 LCD 天气主页
- Wi-Fi 联网后自动定位当前位置
- 和风天气实时天气更新
- 天气图标、天气状态、温度、当前区域、时间、电量和 Wi-Fi 状态显示
- 每 30 分钟自动刷新天气
- NTP 自动校时
- BOOT 长按清除 NVS 并重启

## 已移除内容

- Demo 中的二维码页面
- 语音唤醒、语音识别、Baidu ASR
- 麦克风、I2S、ES8311 音频链路
- 语音模型分区 `model`
- 语音数据分区 `voice_data`
- Camera、IMU、游戏、骰子、木鱼、表情等非天气页面
- TTS、LLM 聊天和扬声器播放流程

## 配置

进入配置菜单：

```powershell
idf.py menuconfig
```

需要配置：

```text
Example Configuration
  QWEATHER REQUEST KEY
  QWEATHER API HOST
```

Wi-Fi 的 SSID 和密码由 ESP-IDF 的 `protocol_examples_common` 配置项提供：

```text
Example Connection Configuration
  WiFi SSID
  WiFi Password
```

## 构建和烧录

分区表已经简化为天气项目使用，建议首次烧录前完整清理：

```powershell
idf.py fullclean
idf.py build
idf.py flash monitor
```

如果烧录后仍出现旧分区或旧日志，先擦除 Flash：

```powershell
idf.py erase-flash
idf.py flash monitor
```

## 正常日志

正常启动后应看到类似日志：

```text
Project name:     sparkbot_lcd_weather
LVGL: Starting LVGL task
ESP-SparkBot-BSP: Backlight on GPIO46 level=1
mmap_assets: Partition 'weather' successfully created
example_common: Connected to example_netif_sta
NET_EVENT_WEATHER
Current region raw: ..., display: Area ...
Temp : [...]
Icon : [...]
Text : [...]
```

取消语音后，不应再看到这些日志：

```text
AFE_SR
wakenet
baidu_asr
ES8311
I2S_IF
MODEL_LOADER
```
