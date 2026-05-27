/*
 * Заголовочный файл приложения XiaoClaw
 */

#ifndef XIAOCLAW_APP_H
#define XIAOCLAW_APP_H

#ifdef __cplusplus
extern "C" {
#endif

// Состояние приложения
typedef enum {
    APP_STATE_IDLE,
    APP_STATE_CONNECTING,
    APP_STATE_CONNECTED,
    APP_STATE_ERROR
} app_state_t;

/**
 * @brief Запуск приложения XiaoClaw
 * 
 * Инициализирует графический интерфейс,
 * подключается к серверу и запускает задачи
 */
void xiaoclaw_app_start(void);

/**
 * @brief Получение текущего состояния приложения
 * 
 * @return app_state_t Текущее состояние
 */
app_state_t xiaoclaw_app_get_state(void);

/**
 * @brief Установка URL сервера XiaoClaw
 * 
 * @param url URL сервера (например, "https://xiaoclaw.com")
 */
void xiaoclaw_app_set_server(const char* url);

#ifdef __cplusplus
}
#endif

#endif // XIAOCLAW_APP_H
