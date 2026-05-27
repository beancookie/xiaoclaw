/*
 * XiaoClaw ESP32-P4 Client - Основное приложение
 * 
 * Реализует:
 * - Графический интерфейс LVGL
 * - Подключение к серверу XiaoClaw
 * - Обработку сенсорного ввода
 * - Аудио взаимодействие
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "lvgl.h"

#include "app_config.h"
#include "xiaoclaw_app.h"
#include "board_config.h"

static const char* TAG = "XiaoClawApp";

// Состояние приложения
typedef enum {
    APP_STATE_IDLE,
    APP_STATE_CONNECTING,
    APP_STATE_CONNECTED,
    APP_STATE_ERROR
} app_state_t;

static app_state_t current_state = APP_STATE_IDLE;

// UI элементы
static lv_obj_t* main_screen = NULL;
static lv_obj_t* status_label = NULL;
static lv_obj_t* connection_panel = NULL;

// Конфигурация сервера
static char server_url[256] = "https://xiaoclaw.com";
static char device_id[64] = "";

// HTTP клиент для связи с сервером
static esp_http_client_handle_t http_client = NULL;

// Callback для HTTP событий
static esp_err_t http_event_handler(esp_http_client_event_t* evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP данные получены (%d байт)", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP запрос завершен");
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP ошибка");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Обновление статуса подключения
static void update_status(const char* status)
{
    if (status_label) {
        lv_label_set_text(status_label, status);
    }
    ESP_LOGI(TAG, "Статус: %s", status);
}

// Инициализация графического интерфейса
static void ui_init(void)
{
    ESP_LOGI(TAG, "Инициализация графического интерфейса...");
    
    // Создаем главный экран
    main_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(main_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x1a1a2e), 0);
    
    // Логотип XiaoClaw
    lv_obj_t* logo_label = lv_label_create(main_screen);
    lv_label_set_text(logo_label, "XiaoClaw");
    lv_obj_set_style_text_color(logo_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(logo_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_margin_bottom(logo_label, 20, 0);
    
    // Статус бар
    lv_obj_t* status_container = lv_obj_create(main_screen);
    lv_obj_set_size(status_container, LV_PCT(80), 60);
    lv_obj_set_style_bg_color(status_container, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(status_container, 0, 0);
    lv_obj_set_flex_flow(status_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t* status_icon = lv_label_create(status_container);
    lv_label_set_text(status_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(status_icon, lv_color_hex(0xffa500), 0);
    lv_obj_set_style_text_font(status_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_margin_right(status_icon, 10, 0);
    
    status_label = lv_label_create(status_container);
    lv_label_set_text(status_label, "Подключение...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);
    
    // Панель подключения
    connection_panel = lv_obj_create(main_screen);
    lv_obj_set_size(connection_panel, LV_PCT(80), 120);
    lv_obj_set_style_bg_color(connection_panel, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_color(connection_panel, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_border_width(connection_panel, 2, 0);
    lv_obj_set_flex_flow(connection_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(connection_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_top(connection_panel, 20, 0);
    
    lv_obj_t* server_label = lv_label_create(connection_panel);
    lv_label_set_text(server_label, "Сервер: xiaoclaw.com");
    lv_obj_set_style_text_color(server_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(server_label, &lv_font_montserrat_14, 0);
    
    // Индикатор загрузки
    lv_obj_t* spinner = lv_spinner_create(connection_panel, 1000, 60);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_arc_width(spinner, 4, 0);
    
    ESP_LOGI(TAG, "Графический интерфейс инициализирован");
}

// Задача для подключения к серверу
static void connect_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Запуск задачи подключения...");
    
    // Генерируем уникальный ID устройства
    uint8_t mac[6];
    esp_read_mac(mac, MAC_ADDRESS_BASE);
    snprintf(device_id, sizeof(device_id), "ESP32P4-%02X%02X%02X%02X", 
             mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "ID устройства: %s", device_id);
    
    // Настраиваем HTTP клиент
    esp_http_client_config_t config = {
        .url = server_url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
        .buffer_size = 2048,
    };
    
    http_client = esp_http_client_init(&config);
    
    // Пытаемся подключиться к серверу
    update_status("Подключение к серверу...");
    
    while (current_state != APP_STATE_CONNECTED) {
        // Отправляем запрос регистрации
        char post_data[512];
        snprintf(post_data, sizeof(post_data), 
                 "{\"device_id\":\"%s\",\"device_type\":\"esp32-p4\",\"version\":\"1.0\"}",
                 device_id);
        
        esp_http_client_set_method(http_client, HTTP_METHOD_POST);
        esp_http_client_set_header(http_client, "Content-Type", "application/json");
        esp_http_client_set_post_field(http_client, post_data, strlen(post_data));
        
        esp_err_t err = esp_http_client_perform(http_client);
        
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(http_client);
            ESP_LOGI(TAG, "HTTP статус: %d", status_code);
            
            if (status_code == 200) {
                current_state = APP_STATE_CONNECTED;
                update_status("✓ Подключено");
                ESP_LOGI(TAG, "Успешное подключение к XiaoClaw!");
                
                // Здесь можно получить токен и другие данные от сервера
                int content_length = esp_http_client_get_content_length(http_client);
                if (content_length > 0) {
                    char* response_buffer = malloc(content_length + 1);
                    if (response_buffer) {
                        int read_len = esp_http_client_read(http_client, response_buffer, content_length);
                        response_buffer[read_len] = '\0';
                        ESP_LOGD(TAG, "Ответ сервера: %s", response_buffer);
                        
                        // Парсим ответ JSON
                        cJSON* json = cJSON_Parse(response_buffer);
                        if (json) {
                            cJSON* token = cJSON_GetObjectItem(json, "token");
                            if (token && token->type == cJSON_String) {
                                ESP_LOGI(TAG, "Получен токен авторизации");
                            }
                            cJSON_Delete(json);
                        }
                        
                        free(response_buffer);
                    }
                }
            } else {
                ESP_LOGW(TAG, "Ошибка подключения (статус %d)", status_code);
                update_status("Ошибка подключения");
            }
        } else {
            ESP_LOGE(TAG, "HTTP ошибка: %s", esp_err_to_name(err));
            update_status("Нет соединения");
        }
        
        // Ждем перед следующей попыткой
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    esp_http_client_cleanup(http_client);
    vTaskDelete(NULL);
}

// Задача для обработки сенсорного ввода
static void touch_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Запуск задачи обработки тачскрина...");
    
    while (1) {
        // LVGL обрабатывает ввод в своей задаче
        // Здесь можно добавить дополнительную логику
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Публичная функция запуска приложения
void xiaoclaw_app_start(void)
{
    ESP_LOGI(TAG, "=== Запуск приложения XiaoClaw ===");
    
    // Инициализируем UI
    ui_init();
    
    // Создаем задачу подключения
    xTaskCreate(connect_task, "connect_task", 4096, NULL, 5, NULL);
    
    // Создаем задачу обработки тачскрина
    xTaskCreate(touch_task, "touch_task", 3072, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "Приложение запущено");
}

// Получение текущего состояния
app_state_t xiaoclaw_app_get_state(void)
{
    return current_state;
}

// Установка URL сервера
void xiaoclaw_app_set_server(const char* url)
{
    strncpy(server_url, url, sizeof(server_url) - 1);
    server_url[sizeof(server_url) - 1] = '\0';
    ESP_LOGI(TAG, "URL сервера обновлен: %s", server_url);
}
