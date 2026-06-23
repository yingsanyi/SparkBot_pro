idf_component_register(
    SRCS "lcd_weather_voice_main.c"
    INCLUDE_DIRS "."
    REQUIRES espressif__esp-sr lcd_weather_ui weather_service sparkbot_audio nvs_flash)
