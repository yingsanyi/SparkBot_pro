idf_component_register(
    SRCS "weather_service.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_event esp_http_client esp_netif esp-tls json nvs_flash)
