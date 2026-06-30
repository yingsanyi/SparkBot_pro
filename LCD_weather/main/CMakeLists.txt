idf_component_register(
    SRCS
        "app_main.c"
        "app/app_display.c"
        "app/app_power.c"
        "app/app_sntp.c"
        "app/app_weather.c"
        "app/app_wifi.c"
        "ui/ui.c"
        "ui/screens/ui_home.c"
        "ui/screens/ui_title.c"
        "ui/fonts/ui_font_ComfortaaBold75.c"
        "ui/fonts/ui_font_OPPOSansH18.c"
        "ui/fonts/ui_font_OPPOSansH25.c"
        "ui/fonts/ui_font_OPPOSansL70.c"
        "ui/images/ui_img_battery_png.c"
        "ui/images/ui_img_wifi_disconnection_png.c"
        "ui/images/ui_img_wifi_png.c"
    INCLUDE_DIRS
        "."
        "./app"
        "./ui"
)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format" "-Wno-unused-variable")


spiffs_create_partition_assets(
    weather
    "../weather"
    FLASH_IN_PROJECT
    MMAP_FILE_SUPPORT_FORMAT ".jpg,.png"
    MMAP_SUPPORT_QOI
)
