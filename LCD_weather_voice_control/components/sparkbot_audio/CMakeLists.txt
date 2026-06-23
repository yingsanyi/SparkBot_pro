idf_component_register(
    SRCS "sparkbot_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES driver espressif__esp_codec_dev)

target_link_libraries(${COMPONENT_LIB} INTERFACE m)
