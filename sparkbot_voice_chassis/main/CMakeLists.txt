idf_component_register(
    SRCS "voice_chassis_main.c"
    INCLUDE_DIRS "."
    REQUIRES espressif__esp-sr sparkbot_audio sparkbot_lcd tracked_chassis_control nvs_flash)
