idf_component_register(SRCS "lcd_color_audio_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_spi sparkbot_audio)
