cmake_minimum_required(VERSION 3.16)

# Проект XiaoClaw ESP32-P4 Client
project(xiaoclaw_esp32_client VERSION 1.0.0 LANGUAGES C CXX ASM)

# Добавляем компоненты ESP-IDF
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/main
)

# Главный исполняемый файл
idf_component_register(SRCS 
    "main/main.c"
    "main/board_config.c"
    "main/xiaoclaw_app.c"
    INCLUDE_DIRS "include"
)

# Требования к компонентам
set(COMPONENTS main driver esp_lcd esp_lvgl_port esp_wifi esp_netif nvs_flash 
               esp_http_client esp_https_server json esp_codec_dev)

# Настройки компиляции
target_compile_definitions(${COMPONENT_TARGET} PRIVATE
    -DPROJECT_VERSION=\"1.0.0\"
    -DCONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_3_4C
)
