cmake_minimum_required(VERSION 3.16)

# Reuse the managed components already available in this workspace when possible.
# Remaining dependencies are resolved from main/idf_component.yml.
set(WORKSPACE_MANAGED_COMPONENTS_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../tank/sparkbot_motion_control/managed_components")

list(APPEND EXTRA_COMPONENT_DIRS
    "${WORKSPACE_MANAGED_COMPONENTS_DIR}/espressif__button"
    "${WORKSPACE_MANAGED_COMPONENTS_DIR}/espressif__esp_mmap_assets"
    "${WORKSPACE_MANAGED_COMPONENTS_DIR}/lvgl__lvgl")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sparkbot_lcd_weather)

idf_component_get_property(esp_lcd_lib esp_lcd COMPONENT_LIB)
target_compile_options(${esp_lcd_lib} PRIVATE -O0)
