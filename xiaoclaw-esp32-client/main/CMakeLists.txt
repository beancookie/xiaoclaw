# ESP-IDF CMakeLists.txt для main компонента

idf_component_register(
    SRCS 
        "main.c"
        "board_config.c"
        "xiaoclaw_app.c"
    INCLUDE_DIRS 
        "."
    REQUIRES
        driver
        esp_lcd
        esp_lvgl_port
        esp_wifi
        esp_netif
        nvs_flash
        esp_http_client
        json
        esp_codec_dev
        lwip
)
