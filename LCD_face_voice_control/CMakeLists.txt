cmake_minimum_required(VERSION 3.16)

set(MANAGED_COMPONENTS_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../tank/sparkbot_motion_control/managed_components")

list(APPEND EXTRA_COMPONENT_DIRS
    "${MANAGED_COMPONENTS_DIR}/espressif__esp-sr"
    "${MANAGED_COMPONENTS_DIR}/espressif__esp-dsp"
    "${MANAGED_COMPONENTS_DIR}/espressif__esp_codec_dev")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lcd_face_voice_control)

# Work around a GCC 14.2 xtensa ICE seen in some ESP-IDF v5.5.x builds while
# keeping the SPI LCD driver available for this project.
idf_component_get_property(esp_lcd_lib esp_lcd COMPONENT_LIB)
target_compile_options(${esp_lcd_lib} PRIVATE -O0)
