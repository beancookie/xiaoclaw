/*
 * Команды инициализации для дисплея JD9365
 * Плата: Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C
 * Дисплей: 800x800 IPS
 */

#include "esp_lcd_jd9365.h"

// Команды инициализации для JD9365 (800x800)
const jd9365_lcd_init_cmd_t lcd_init_cmds[] = {
    // Page 0
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    
    // Enable command set B
    {0xE1, (uint8_t[]){0x93}, 1, 0},
    {0xE2, (uint8_t[]){0x65}, 1, 0},
    {0xE3, (uint8_t[]){0xF8}, 1, 0},
    {0x80, (uint8_t[]){0x01}, 1, 0},
    
    // Page 1
    {0xE0, (uint8_t[]){0x01}, 1, 0},
    
    // AVDD, AVCL, VGH, VGL voltage settings
    {0x00, (uint8_t[]){0x00}, 1, 0},
    {0x01, (uint8_t[]){0x41}, 1, 0},
    {0x03, (uint8_t[]){0x10}, 1, 0},
    {0x04, (uint8_t[]){0x44}, 1, 0},
    
    // Source timing settings
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x18, (uint8_t[]){0xD0}, 1, 0},
    {0x19, (uint8_t[]){0x00}, 1, 0},
    {0x1A, (uint8_t[]){0x00}, 1, 0},
    {0x1B, (uint8_t[]){0xD0}, 1, 0},
    {0x1C, (uint8_t[]){0x00}, 1, 0},
    
    // Display timing settings
    {0x24, (uint8_t[]){0xFE}, 1, 0},
    {0x35, (uint8_t[]){0x26}, 1, 0},
    
    // Gamma settings
    {0x37, (uint8_t[]){0x09}, 1, 0},
    
    // VCOM settings
    {0x38, (uint8_t[]){0x04}, 1, 0},
    {0x39, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x0A}, 1, 0},
    {0x3C, (uint8_t[]){0x78}, 1, 0},
    {0x3D, (uint8_t[]){0xFF}, 1, 0},
    {0x3E, (uint8_t[]){0xFF}, 1, 0},
    {0x3F, (uint8_t[]){0xFF}, 1, 0},
    
    // Gate timing
    {0x40, (uint8_t[]){0x00}, 1, 0},
    {0x41, (uint8_t[]){0x64}, 1, 0},
    {0x42, (uint8_t[]){0xC7}, 1, 0},
    {0x43, (uint8_t[]){0x18}, 1, 0},
    {0x44, (uint8_t[]){0x0B}, 1, 0},
    {0x45, (uint8_t[]){0x14}, 1, 0},
    
    // EQ settings
    {0x55, (uint8_t[]){0x02}, 1, 0},
    {0x57, (uint8_t[]){0x49}, 1, 0},
    {0x59, (uint8_t[]){0x0A}, 1, 0},
    {0x5A, (uint8_t[]){0x1B}, 1, 0},
    {0x5B, (uint8_t[]){0x19}, 1, 0},
    
    // PWR control
    {0x5D, (uint8_t[]){0x7F}, 1, 0},
    {0x5E, (uint8_t[]){0x56}, 1, 0},
    {0x5F, (uint8_t[]){0x43}, 1, 0},
    {0x60, (uint8_t[]){0x37}, 1, 0},
    {0x61, (uint8_t[]){0x33}, 1, 0},
    {0x62, (uint8_t[]){0x25}, 1, 0},
    {0x63, (uint8_t[]){0x2A}, 1, 0},
    {0x64, (uint8_t[]){0x16}, 1, 0},
    {0x65, (uint8_t[]){0x30}, 1, 0},
    {0x66, (uint8_t[]){0x2B}, 1, 0},
    {0x67, (uint8_t[]){0x20}, 1, 0},
    {0x68, (uint8_t[]){0x1C}, 1, 0},
    {0x69, (uint8_t[]){0x1A}, 1, 0},
    {0x6A, (uint8_t[]){0x18}, 1, 0},
    {0x6B, (uint8_t[]){0x16}, 1, 0},
    {0x6C, (uint8_t[]){0x04}, 1, 0},
    {0x6D, (uint8_t[]){0x02}, 1, 0},
    {0x6E, (uint8_t[]){0x01}, 1, 0},
    {0x6F, (uint8_t[]){0x00}, 1, 0},
    
    // CABC settings
    {0x70, (uint8_t[]){0x05}, 1, 0},
    {0x71, (uint8_t[]){0x07}, 1, 0},
    {0x72, (uint8_t[]){0x09}, 1, 0},
    {0x73, (uint8_t[]){0x0B}, 1, 0},
    {0x74, (uint8_t[]){0x0D}, 1, 0},
    {0x75, (uint8_t[]){0x0F}, 1, 0},
    {0x76, (uint8_t[]){0x11}, 1, 0},
    {0x77, (uint8_t[]){0x13}, 1, 0},
    
    // MIPI settings
    {0xB0, (uint8_t[]){0x02}, 1, 0},
    {0xB1, (uint8_t[]){0x02}, 1, 0},
    {0xB2, (uint8_t[]){0x08}, 1, 0},
    {0xB3, (uint8_t[]){0x08}, 1, 0},
    {0xB4, (uint8_t[]){0x08}, 1, 0},
    {0xB5, (uint8_t[]){0x08}, 1, 0},
    {0xB6, (uint8_t[]){0x22}, 1, 0},
    {0xB7, (uint8_t[]){0x22}, 1, 0},
    {0xB8, (uint8_t[]){0x08}, 1, 0},
    {0xB9, (uint8_t[]){0x08}, 1, 0},
    
    // Frame rate
    {0xBD, (uint8_t[]){0x01}, 1, 0},
    {0xBE, (uint8_t[]){0x57}, 1, 0},
    
    // Page 0 again
    {0xE0, (uint8_t[]){0x00}, 1, 0},
    
    // Display ON
    {0x29, NULL, 0, 0},
    
    // Sleep OUT
    {0x11, NULL, 0, 120},
};
