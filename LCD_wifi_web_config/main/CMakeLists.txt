idf_component_register(
    SRCS "lcd_wifi_web_config_main.c"
    INCLUDE_DIRS "."
    REQUIRES lcd_face_ui esp_wifi esp_http_server nvs_flash esp_netif esp_event
    EMBED_TXTFILES "index.html")
