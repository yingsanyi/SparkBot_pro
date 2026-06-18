idf_component_register(SRCS "lcd_blink_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd esp_driver_gpio esp_driver_spi)
