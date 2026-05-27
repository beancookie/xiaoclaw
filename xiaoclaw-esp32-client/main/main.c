/*
 * XiaoClaw ESP32-P4 Client - Главный файл инициализации
 * Плата: Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C
 * 
 * Этот файл является точкой входа приложения
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "xiaoclaw_app.h"
#include "board_config.h"

static const char* TAG = "XiaoClaw";

void app_main(void)
{
    ESP_LOGI(TAG, "=================================");
    ESP_LOGI(TAG, "XiaoClaw ESP32-P4 Client v1.0");
    ESP_LOGI(TAG, "Плата: Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C");
    ESP_LOGI(TAG, "Дисплей: %dx%d (JD9365)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    ESP_LOGI(TAG, "=================================");

    // Инициализация NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS требует очистки. Перезапускаем...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Инициализация платы (дисплей, тачскрин, аудио, WiFi)
    ESP_LOGI(TAG, "Инициализация периферии...");
    board_init();

    // Запуск основного приложения XiaoClaw
    ESP_LOGI(TAG, "Запуск приложения XiaoClaw...");
    xiaoclaw_app_start();

    ESP_LOGI(TAG, "Система запущена успешно!");
}
