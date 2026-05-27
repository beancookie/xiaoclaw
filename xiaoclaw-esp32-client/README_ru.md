# XiaoClaw ESP32-P4 Client для Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C

Клиентское устройство для системы **XiaoClaw** на базе платы **Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C**.

## 📋 Характеристики платы

- **Процессор**: ESP32-P4 (двухъядерный RISC-V, до 400 MHz)
- **Дисплей**: 3.4" IPS, 800×800 пикселей, контроллер JD9365
- **Тачскрин**: Ёмкостный, контроллер GT911
- **WiFi**: WiFi 6 (802.11ax) через внешний модуль ESP32-C6
- **Аудио**: Кодеки ES8311 (спикер) + ES7210 (микрофон)
- **Камера**: Интерфейс CSI (опционально)
- **Память**: 16MB PSRAM

## 🔧 Конфигурация пинов

| Компонент | Пин GPIO | Описание |
|-----------|----------|----------|
| **Дисплей** | | |
| LCD Reset | GPIO27 | Сброс дисплея |
| Backlight | GPIO26 | Подсветка (инвертированная) |
| MIPI DSI | - | 2 линии данных, 1500 Mbps |
| **Тачскрин** | | |
| Touch Reset | GPIO23 | Сброс тачскрина |
| Touch I2C | GPIO7/8 | SDA/SCL (общая шина с аудио) |
| **Аудио** | | |
| I2S MCLK | GPIO13 | Тактовая частота |
| I2S BCLK | GPIO12 | Битовая частота |
| I2S WS | GPIO10 | Синхронизация кадров |
| I2S DIN | GPIO11 | Вход данных |
| I2S DOUT | GPIO9 | Выход данных |
| Codec PA | GPIO53 | Усилитель динамика |
| Codec I2C | GPIO7/8 | Шина управления |
| **Кнопки** | | |
| Boot Button | GPIO35 | Кнопка запуска |

## 🏗️ Архитектура проекта

```
xiaoclaw-esp32-client/
├── main/                    # Исходный код приложения
│   ├── CMakeLists.txt      # Конфигурация сборки
│   ├── main.c              # Точка входа
│   ├── board_config.c      # Инициализация платы
│   └── xiaoclaw_app.c      # Логика приложения
├── include/                 # Заголовочные файлы
│   ├── app_config.h        # Конфигурация платы
│   └── xiaoclaw_app.h      # Заголовки приложения
├── CMakeLists.txt          # Главный CMake
├── sdkconfig.defaults      # Настройки ESP-IDF
└── README.md               # Этот файл
```

## ⚙️ Требования

- **ESP-IDF**: v5.3 или новее
- **Целевая платформа**: ESP32-P4
- **Компоненты**:
  - LVGL v8.3 (графический интерфейс)
  - ESP LCD (драйверы дисплея)
  - ESP Audio (аудио кодеки)

## 🚀 Быстрый старт

### 1. Установка ESP-IDF v5.3

```bash
# Клонируем ESP-IDF
git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32p4
source export.sh
```

### 2. Настройка проекта

```bash
cd /workspace/xiaoclaw-esp32-client

# Выбираем целевую платформу
idf.py set-target esp32p4

# Открываем меню конфигурации
idf.py menuconfig
```

В меню конфигурации убедитесь:
- **Board type**: `Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C`
- **Display resolution**: `800x800`
- **Touch controller**: `GT911`

### 3. Сборка и прошивка

```bash
# Собираем проект
idf.py build

# Прошиваем (замените /dev/ttyUSB0 на ваш порт)
idf.py -p /dev/ttyUSB0 flash monitor
```

## 🌐 Подключение к XiaoClaw

После прошивки:

1. Устройство автоматически подключится к WiFi (настройте SSID/пароль в menuconfig)
2. На экране появится интерфейс подключения
3. Введите сервер XiaoClaw: `https://xiaoclaw.com`
4. Авторизуйтесь со своим аккаунтом

## 📖 Документация

- [Официальный сайт XiaoClaw](https://xiaoclaw.com)
- [GitHub репозиторий](https://github.com/huafenchi/xiaoclaw-client)
- [Wiki платы Waveshare](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-3.4C)
- [Даташит платы](https://files.waveshare.com/wiki/ESP32-P4-WIFI6/ESP32-P4-WIFI6-datasheet.pdf)
- [Документация ESP-IDF v5.3](https://docs.espressif.com/projects/esp-idf/en/v5.3/)

## ⚠️ Важные замечания

1. **WiFi модуль**: ESP32-P4 не имеет встроенного WiFi/BT. На плате установлен внешний модуль ESP32-C6 для WiFi 6.

2. **Питание**: Используйте качественный блок питания 5V/2A минимум.

3. **Отладка**: Для отладки используйте USB-C порт (не путать с портом для прошивки).

## 🛠️ Разработка

### Структура кода

- `board_config.c` - Инициализация периферии (дисплей, тачскрин, аудио)
- `xiaoclaw_app.c` - Основная логика клиента XiaoClaw
- `main.c` - Точка входа, запуск задач FreeRTOS

### Добавление функций

1. Измените `include/app_config.h` при необходимости
2. Добавьте код в соответствующие модули
3. Пересоберите проект

## 📄 Лицензия

Этот проект является частью экосистемы XiaoClaw.

## 👥 Авторы

- Оригинальный проект: [XiaoClaw](https://github.com/huafenchi/xiaoclaw-client)
- Адаптация для ESP32-P4: Сообщество

## 🆘 Поддержка

При возникновении проблем:
1. Проверьте логи через `idf.py monitor`
2. Убедитесь в правильности подключения платы
3. Проверьте настройки WiFi в menuconfig
4. Обратитесь в сообщество XiaoClaw
