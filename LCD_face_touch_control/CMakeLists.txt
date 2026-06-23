cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lcd_face_touch_control)

# Work around a GCC 14.2 xtensa ICE seen in some ESP-IDF v5.5.x builds while
# keeping the SPI LCD driver available for this project.
idf_component_get_property(esp_lcd_lib esp_lcd COMPONENT_LIB)
target_compile_options(${esp_lcd_lib} PRIVATE -O0)
