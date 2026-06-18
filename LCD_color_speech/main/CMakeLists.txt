idf_component_register(SRCS "lcd_color_speech_main.c"
                       INCLUDE_DIRS "."
                       EMBED_FILES "../assets/red.wav"
                                   "../assets/green.wav"
                                   "../assets/blue.wav"
                                   "../assets/yellow.wav"
                                   "../assets/purple.wav"
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_spi sparkbot_audio)
