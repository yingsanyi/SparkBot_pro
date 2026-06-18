# ESP-SparkBot LCD Color Audio

This ESP-IDF project combines the earlier LCD color examples with the
ESP-SparkBot ES8311 speaker path. The LCD cycles through solid colors while the
speaker plays matching generated sine-wave tones.

See `LCD_COLOR_AUDIO_LESSON.md` for the Chinese lesson notes and code
explanation.

## Build

```powershell
idf.py set-target esp32s3
idf.py build
```

## Flash

```powershell
idf.py -p COMx flash monitor
```
