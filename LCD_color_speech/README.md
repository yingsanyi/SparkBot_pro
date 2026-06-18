# ESP-SparkBot LCD Color Speech

这个 ESP-IDF 工程会循环刷新 LCD 背景色，同时在屏幕中央显示对应的中文颜色名，并通过喇叭播放这个颜色名的语音。

默认循环内容：

- 红色
- 绿色
- 蓝色
- 黄色
- 紫色

显示部分沿用前面 LCD 项目的 `esp_lcd_panel_draw_bitmap()` 方式，不引入 LVGL。中文字形由 `tools/generate_assets.ps1` 生成到 `main/color_name_glyphs.inc`。语音由 Windows 中文 TTS 生成到 `assets/*.wav`，然后通过 ESP-IDF 的 `EMBED_FILES` 嵌入固件。

## Configure

首次构建前设置目标芯片：

```sh
idf.py set-target esp32s3
```

可在 `idf.py menuconfig` 的 `LCD Color Speech Configuration` 中调整：

- `CONFIG_LCD_COLOR_SPEECH_HOLD_MS`：每种颜色播放完后保持的时间，默认 2500 ms。
- `CONFIG_LCD_COLOR_SPEECH_VOLUME`：喇叭音量，默认 75。

## Generate Assets

如果需要重新生成中文字形和语音文件：

```powershell
powershell -ExecutionPolicy Bypass -File tools\generate_assets.ps1
```

## Build And Flash

```sh
idf.py -p PORT flash monitor
```
