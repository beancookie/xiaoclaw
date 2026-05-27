# Конфигурация платы Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C

#ifndef XIAOCLAW_CONFIG_H
#define XIAOCLAW_CONFIG_H

#include <driver/gpio.h>

// ============================================
// Конфигурация для Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C
// Спецификация: https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-3.4C
// Документация: https://files.waveshare.com/wiki/ESP32-P4-WIFI6/ESP32-P4-WIFI6-datasheet.pdf
// ============================================

// --- Дисплей (800x800, контроллер JD9365) ---
#define DISPLAY_WIDTH                   800
#define DISPLAY_HEIGHT                  800
#define PIN_NUM_LCD_RST                 GPIO_NUM_27
#define DISPLAY_BACKLIGHT_PIN           GPIO_NUM_26
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true
#define LCD_MIPI_DSI_LANE_BITRATE_MBPS  1500
#define LCD_BIT_PER_PIXEL               16
#define LCD_MIPI_DSI_LANE_NUM           2

// Настройки отображения
#define DISPLAY_SWAP_XY                 false
#define DISPLAY_MIRROR_X                false
#define DISPLAY_MIRROR_Y                false
#define DISPLAY_OFFSET_X                0
#define DISPLAY_OFFSET_Y                0

// MIPI DSI PHY питание
#define MIPI_DSI_PHY_PWR_LDO_CHAN       3
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

// --- Аудио кодек (ES8311 + ES7210) ---
#define AUDIO_INPUT_SAMPLE_RATE         24000
#define AUDIO_OUTPUT_SAMPLE_RATE        24000
#define AUDIO_INPUT_REFERENCE           true

#define AUDIO_I2S_GPIO_MCLK             GPIO_NUM_13
#define AUDIO_I2S_GPIO_WS               GPIO_NUM_10
#define AUDIO_I2S_GPIO_BCLK             GPIO_NUM_12
#define AUDIO_I2S_GPIO_DIN              GPIO_NUM_11
#define AUDIO_I2S_GPIO_DOUT             GPIO_NUM_9

#define AUDIO_CODEC_PA_PIN              GPIO_NUM_53
#define AUDIO_CODEC_I2C_SDA_PIN         GPIO_NUM_7
#define AUDIO_CODEC_I2C_SCL_PIN         GPIO_NUM_8
#define AUDIO_CODEC_ES8311_ADDR         0x43  // ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR         0x82  // ES7210_CODEC_DEFAULT_ADDR

// --- Кнопки ---
#define BOOT_BUTTON_GPIO                GPIO_NUM_35

// --- Тачскрин (GT911) ---
#define TOUCH_PANEL_RST_GPIO            GPIO_NUM_23
#define TOUCH_PANEL_INT_GPIO            GPIO_NUM_NC  // Не используется (опрос по таймеру)
#define TOUCH_I2C_ADDRESS               0x5D  // Основной адрес GT911
#define TOUCH_I2C_ADDRESS_BACKUP        0x14  // Резервный адрес GT911

// --- Камера (CSI интерфейс) ---
#define CAMERA_XCLK_GPIO                GPIO_NUM_NC
#define CAMERA_PWDN_GPIO                GPIO_NUM_NC
#define CAMERA_RESET_GPIO               GPIO_NUM_NC

// --- WiFi (внешний модуль ESP32-C6) ---
// Примечание: ESP32-P4 не имеет встроенного WiFi/BT
// WiFi 6 реализован через внешний модуль ESP32-C6 на плате
#define WIFI_EXTERNAL_MODULE            1

#endif // XIAOCLAW_CONFIG_H
