# ESP-SparkBot LCD Text Scroll

这个 ESP-IDF 工程基于 `LCD_text_switch` 改进：当文字长度超过 240x240 LCD 的显示宽度时，不再静态居中显示，而是让整句文字从右向左滚动。

屏幕显示内容：

```text
HelloWorld， 你好世界！
```

中文字符仍然沿用 `LCD_text_switch` 的固定字形表，标点使用同一行内的矢量小图形绘制。这样可以继续保持 `LCD_blink` 风格的底层 `esp_lcd_panel_draw_bitmap()` 刷屏方式，不需要额外引入 LVGL。

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

滚动速度由两个配置项控制，可以通过 `idf.py menuconfig` 在 `LCD Text Scroll Configuration` 中修改：

- `CONFIG_LCD_TEXT_SCROLL_FRAME_MS`：每一帧之间的延时，默认 40 ms。
- `CONFIG_LCD_TEXT_SCROLL_PIXELS_PER_FRAME`：每一帧向左移动的像素数，默认 2 px。

`sdkconfig.defaults` 里默认设置了 `CONFIG_COMPILER_OPTIMIZATION_NONE=y`，用于避开当前 ESP-IDF v5.4.1/GCC14 在公共 `esp_lcd` RGB 驱动中可能遇到的 internal compiler error。本工程实际使用的是 SPI ST7789。

## Build And Flash

```sh
idf.py -p PORT flash monitor
```

串口日志会显示滚动文字的总宽度和动画启动状态。
