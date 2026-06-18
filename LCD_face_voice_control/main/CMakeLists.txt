idf_component_register(
    SRCS "lcd_face_voice_main.c"
    INCLUDE_DIRS "."
    REQUIRES espressif__esp-sr lcd_face_ui sparkbot_audio nvs_flash)
