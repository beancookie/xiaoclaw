/*
 * Заголовочный файл конфигурации платы
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <driver/ledc.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch.h>
#include <esp_lvgl_port.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация платы
 * 
 * Выполняет инициализацию:
 * - I2C шины
 * - Дисплея (MIPI DSI, JD9365)
 * - Тачскрина (GT911)
 * - Подсветки
 */
void board_init(void);

/**
 * @brief Установка яркости подсветки дисплея
 * 
 * @param brightness Яркость в процентах (0-100)
 */
void board_set_backlight(uint8_t brightness);

/**
 * @brief Получение обработчика дисплея LVGL
 * 
 * @return lv_display_t* Обработчик дисплея
 */
lv_display_t* board_get_lvgl_display(void);

/**
 * @brief Получение обработчика тачскрина LVGL
 * 
 * @return lv_indev_t* Обработчик тачскрина
 */
lv_indev_t* board_get_lvgl_touch(void);

#ifdef __cplusplus
}
#endif

#endif // BOARD_CONFIG_H
