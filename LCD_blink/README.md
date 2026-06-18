# ESP-SparkBot LCD Blink Test

This ESP-IDF project initializes the ESP-SparkBot head LCD and cycles the full
240x240 ST7789 panel through solid colors. It is intended as a simple hardware
bring-up test for the LCD SPI bus, panel initialization, and backlight control.

## Hardware

- ESP-SparkBot head hardware with ESP32-S3
- 240x240 ST7789 LCD connected to the ESP-SparkBot BSP pinout
- USB cable for power, flashing, and serial monitor

LCD connections used by this project:

| Signal | GPIO |
| ------ | ---- |
| MOSI | GPIO 47 |
| SCLK | GPIO 21 |
| CS | GPIO 44 |
| DC | GPIO 43 |
| RESET | Not connected |
| Backlight | GPIO 46 |

The panel is configured for `SPI3_HOST`, SPI mode 2, RGB565 color, 240x240
resolution, and an 80 MHz pixel clock, matching the ESP-SparkBot BSP.

## Configure

Set the target before building:

```sh
idf.py set-target esp32s3
```

The color-change period is controlled by `CONFIG_LCD_BLINK_PERIOD_MS` in
`idf.py menuconfig` under `LCD Blink Configuration`. The default is 1000 ms.

## Build And Flash

```sh
idf.py -p PORT flash monitor
```

Expected monitor output includes lines similar to:

```text
I lcd_blink: LCD initialized, start color blink
I lcd_blink: LCD pattern: red
I lcd_blink: LCD pattern: green
I lcd_blink: LCD pattern: blue
I lcd_blink: LCD pattern: white
I lcd_blink: LCD pattern: black
```

If the LCD does not change, verify that the hardware is the ESP-SparkBot head
module, the LCD FPC is seated, and GPIO 46 drives the backlight.
