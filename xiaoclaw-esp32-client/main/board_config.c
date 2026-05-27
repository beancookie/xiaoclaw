/*
 * Конфигурация и инициализация платы Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C
 * 
 * Включает инициализацию:
 * - Дисплея (MIPI DSI, JD9365)
 * - Тачскрина (GT911)
 * - Аудио кодеков (ES8311, ES7210)
 * - WiFi (ESP32-C6 внешний модуль)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_jd9365.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "sdkconfig.h"

#include "app_config.h"
#include "board_config.h"

static const char* TAG = "BoardConfig";

// Глобальные обработчики
static i2c_master_bus_handle_t i2c_bus = NULL;
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_touch_handle_t touch_panel = NULL;
static lv_display_t* lvgl_disp = NULL;
static lv_indev_t* lvgl_touch = NULL;

// Инициализация I2C шины для аудио и тачскрина
static void i2c_init(void)
{
    ESP_LOGI(TAG, "Инициализация I2C шины...");
    
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
        .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };
    
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));
    ESP_LOGI(TAG, "I2C шина инициализирована (SDA=%d, SCL=%d)", 
             AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN);
}

// Проверка наличия устройства на I2C
static esp_err_t i2c_device_probe(uint8_t addr)
{
    return i2c_master_probe(i2c_bus, addr, 100);
}

// Инициализация дисплея через MIPI DSI
static void display_init(void)
{
    ESP_LOGI(TAG, "Инициализация дисплея JD9365 (%dx%d)...", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    
    // Включаем питание PHY
    // LDO канал 3, 2.5V для MIPI DSI PHY
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
    ESP_LOGI(TAG, "MIPI DSI PHY включен");
    
    // Создаем шину DSI
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = LCD_MIPI_DSI_LANE_NUM,
        .lane_bit_rate_mbps = LCD_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));
    
    // Создаем DBI интерфейс для отправки команд
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &lcd_io));
    
    // Настраиваем DPI панель
    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 46,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,
        .video_timing = {
            .h_size = DISPLAY_WIDTH,
            .v_size = DISPLAY_HEIGHT,
            .hsync_pulse_width = 20,
            .hsync_back_porch = 20,
            .hsync_front_porch = 40,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 12,
            .vsync_front_porch = 24,
        },
        .flags = {
            .use_dma2d = true,
        },
    };
    
    // Команды инициализации для JD9365
    extern const jd9365_lcd_init_cmd_t lcd_init_cmds[];
    jd9365_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = LCD_MIPI_DSI_LANE_NUM,
        },
    };
    
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365(lcd_io, &panel_config, &lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
    
    // Создаем LVGL дисплей
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .control_handle = NULL,
        .buffer_size = DISPLAY_WIDTH * DISPLAY_HEIGHT / 2,
        .double_buffer = true,
        .trans_size = 0,
        .hres = DISPLAY_WIDTH,
        .vres = DISPLAY_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = DISPLAY_SWAP_XY,
            .mirror_x = DISPLAY_MIRROR_X,
            .mirror_y = DISPLAY_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,
        },
    };
    
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_LOGI(TAG, "Дисплей инициализирован успешно");
}

// Инициализация тачскрина GT911
static void touch_init(void)
{
    ESP_LOGI(TAG, "Инициализация тачскрина GT911...");
    
    // Проверяем наличие тачскрина на I2C
    uint8_t touch_addr = TOUCH_I2C_ADDRESS;
    if (i2c_device_probe(TOUCH_I2C_ADDRESS) != ESP_OK) {
        if (i2c_device_probe(TOUCH_I2C_ADDRESS_BACKUP) == ESP_OK) {
            touch_addr = TOUCH_I2C_ADDRESS_BACKUP;
            ESP_LOGI(TAG, "Тачскрин найден по резервному адресу: 0x%02X", touch_addr);
        } else {
            ESP_LOGE(TAG, "Тачскрин не найден на I2C шине!");
            return;
        }
    } else {
        ESP_LOGI(TAG, "Тачскрин найден по адресу: 0x%02X", touch_addr);
    }
    
    // Настраиваем тачскрин
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = DISPLAY_WIDTH,
        .y_max = DISPLAY_HEIGHT,
        .rst_gpio_num = TOUCH_PANEL_RST_GPIO,
        .int_gpio_num = TOUCH_PANEL_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = DISPLAY_SWAP_XY,
            .mirror_x = DISPLAY_MIRROR_X,
            .mirror_y = DISPLAY_MIRROR_Y,
        },
    };
    
    // Создаем I2C интерфейс для тачскрина
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.dev_addr = touch_addr;
    tp_io_config.scl_speed_hz = 400000;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle));
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_panel));
    
    // Добавляем тачскрин в LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_panel,
    };
    lvgl_touch = lvgl_port_add_touch(&touch_cfg);
    
    ESP_LOGI(TAG, "Тачскрин инициализирован успешно");
}

// Инициализация подсветки дисплея
void board_set_backlight(uint8_t brightness)
{
    static bool pwm_initialized = false;
    static ledc_channel_config_t ledc_channel;
    
    if (!pwm_initialized) {
        // Настраиваем PWM для подсветки
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .timer_num = LEDC_TIMER_0,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
        
        ledc_channel = (ledc_channel_config_t){
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = DISPLAY_BACKLIGHT_PIN,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
        pwm_initialized = true;
    }
    
    // Устанавливаем яркость (0-100%)
    uint32_t duty = DISPLAY_BACKLIGHT_OUTPUT_INVERT ? 
                    (1023 - (brightness * 1023 / 100)) : 
                    (brightness * 1023 / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// Публичная функция инициализации платы
void board_init(void)
{
    ESP_LOGI(TAG, "=== Начало инициализации платы ===");
    
    // 1. I2C шина
    i2c_init();
    
    // 2. Дисплей
    display_init();
    
    // 3. Тачскрин
    touch_init();
    
    // 4. Подсветка (включаем на 80%)
    board_set_backlight(80);
    
    // 5. Аудио кодек (инициализируется в xiaoclaw_app)
    ESP_LOGI(TAG, "Аудио кодек будет инициализирован в приложении");
    
    // 6. WiFi (инициализируется в xiaoclaw_app)
    ESP_LOGI(TAG, "WiFi будет инициализирован в приложении");
    
    ESP_LOGI(TAG, "=== Инициализация платы завершена ===");
}

// Получение обработчика дисплея LVGL
lv_display_t* board_get_lvgl_display(void)
{
    return lvgl_disp;
}

// Получение обработчика тачскрина LVGL
lv_indev_t* board_get_lvgl_touch(void)
{
    return lvgl_touch;
}
