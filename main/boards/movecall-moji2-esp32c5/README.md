<<<<<<< HEAD
# Build and Configuration Guide

This document provides instructions on how to configure and build the firmware for **Movecall Moji2.0 (Xiaozhi AI Edition)**.

## 🛠 Prerequisites
*   **ESP-IDF Version**: v5.5
*   **Target Chip**: ESP32-C5

## 🔗 Hardware Information
This project is based on the following open-source hardware:
*   **OSHWHub Link**: [https://oshwhub.com/movecall/moji2](https://oshwhub.com/movecall/moji2)

---

## 🚀 Build Steps

### 1. Set the Build Target
Initialize the project to target the ESP32-C5 chip:
```bash
idf.py set-target esp32c5
```

### 2. Configure the Board Type
Open the graphical configuration menu:
```bash
idf.py menuconfig
```

**Navigate to the following path to select your board:**
> **Xiaozhi Assistant** -> **Board Type** -> **Movecall Moji2.0 小智AI衍生版**

*Note: After selecting, press **S** to save (then Enter to confirm) and press **Q** to exit.*

### 3. Build the Project
Run the following command to start the compilation:
```bash
idf.py build
```

---

## 🔧 Useful Commands

**Clean Build Files (Recommended if you encounter errors):**
```bash
idf.py fullclean
```

**Flash Firmware to Device:**
```bash
idf.py flash
```

**Monitor Serial Output:**
```bash
idf.py monitor
=======
# Build and Configuration Guide

This document provides instructions on how to configure and build the firmware for **Movecall Moji2.0 (Xiaozhi AI Edition)**.

## 🛠 Prerequisites
*   **ESP-IDF Version**: v5.5
*   **Target Chip**: ESP32-C5

## 🔗 Hardware Information
This project is based on the following open-source hardware:
*   **OSHWHub Link**: [https://oshwhub.com/movecall/moji2](https://oshwhub.com/movecall/moji2)

---

## 🚀 Build Steps

### 1. Set the Build Target
Initialize the project to target the ESP32-C5 chip:
```bash
idf.py set-target esp32c5
```

### 2. Configure the Board Type
Open the graphical configuration menu:
```bash
idf.py menuconfig
```

**Navigate to the following path to select your board:**
> **Xiaozhi Assistant** -> **Board Type** -> **Movecall Moji2.0 小智AI衍生版**

*Note: After selecting, press **S** to save (then Enter to confirm) and press **Q** to exit.*

### 3. Build the Project
Run the following command to start the compilation:
```bash
idf.py build
```

---

## 🔧 Useful Commands

**Clean Build Files (Recommended if you encounter errors):**
```bash
idf.py fullclean
```

**Flash Firmware to Device:**
```bash
idf.py flash
```

**Monitor Serial Output:**
```bash
idf.py monitor
>>>>>>> upstream/main
```