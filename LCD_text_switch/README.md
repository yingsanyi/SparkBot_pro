# ESP-SparkBot LCD Text Switch

这个 ESP-IDF 工程基于 `LCD_blink` 改写：初始化 ESP-SparkBot 头部
240x240 ST7789 LCD 后，不再切换纯色或闪烁背光，而是在屏幕中间循环显示：

- `HelloWorld`
- `你好世界`

中文显示方式参考了官方 `esp_sparkbot-master/example/bilibili_fans`
里的 LVGL 字体做法：官方例子用真实中文字体预生成字形表，再用
`lv_label_set_text()` 和 `lv_obj_set_style_text_font()` 显示中文。本工程为了
保持 `LCD_blink` 的低层 `esp_lcd_panel_draw_bitmap()` 结构，没有搬入整套
LVGL，而是内置了 `你好世界` 四个字的固定字形表。

## Hardware

LCD 引脚保持和 `LCD_blink` 一致：

| Signal | GPIO |
| ------ | ---- |
| MOSI | GPIO 47 |
| SCLK | GPIO 21 |
| CS | GPIO 44 |
| DC | GPIO 43 |
| RESET | Not connected |
| Backlight | GPIO 46 |

## Configure

首次构建前设置目标芯片：

```sh
idf.py set-target esp32s3
```

文字切换周期由 `CONFIG_LCD_TEXT_SWITCH_PERIOD_MS` 控制，默认 1000 ms。
可以通过 `idf.py menuconfig` 在 `LCD Text Switch Configuration` 中修改。

`sdkconfig.defaults` 里默认设置了 `CONFIG_COMPILER_OPTIMIZATION_NONE=y`。
这是为了避开当前 ESP-IDF v5.4.1/GCC14 在编译公共 `esp_lcd` RGB 驱动时
可能出现的 internal compiler error；本工程实际使用的是 SPI ST7789。

## Build And Flash

```sh
idf.py -p PORT flash monitor
```

串口日志会显示当前正在刷新的文字画面，例如：

```text
I lcd_text: Show HelloWorld
I lcd_text: Show Chinese text
```
