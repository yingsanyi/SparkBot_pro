# ESP-SparkBot LCD Color Frequency

This ESP-IDF project is an advanced version of `LCD_blink`. It initializes the
ESP-SparkBot 240x240 ST7789 LCD, fills the screen with programmable RGB565
colors, and changes the LCD backlight blink frequency for each color mode.

See `LCD_COLOR_FREQUENCY_ADVANCED.md` for the detailed Chinese lesson notes,
modified code sections, and principles.

## Build

```powershell
idf.py set-target esp32s3
idf.py build
```

## Flash

```powershell
idf.py -p COMx flash monitor
```

Replace `COMx` with the actual serial port.
