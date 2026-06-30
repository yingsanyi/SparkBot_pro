idf_component_register(
    SRCS "lcd_blink_main.c"
    REQUIRES esp_lcd esp_driver_gpio esp_driver_spi
    INCLUDE_DIRS "."
)
