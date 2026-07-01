idf_component_register(
    SRCS "sparkbot_web_car_control_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_http_server esp_netif esp_event nvs_flash tracked_chassis_control lcd_face_ui
    EMBED_TXTFILES "index.html")