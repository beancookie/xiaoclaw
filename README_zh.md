# XiaoClaw: 带本地 Agent 大脑的 AI 语音助手

<p align="center">
  <strong>ESP32-S3 AI 语音助手 — 语音 I/O + 本地 LLM Agent</strong>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
  <a href="https://github.com/anthropics/claude-code"><img src="https://img.shields.io/badge/Claude-Opus_4.6-blue.svg" alt="Claude"></a>
</p>

---

## 介绍

**XiaoClaw** 是一个统一的 ESP32-S3 固件，将语音交互与本地 AI Agent 大脑结合在一起。它整合了：

- **xiaozhi-esp32** — 语音 I/O 层：音频录制、播放、唤醒词检测、显示屏、网络通信
- **mimiclaw** — Agent 大脑：LLM 推理、工具调用、记忆管理、自主任务执行

所有功能运行在单个 ESP32-S3 芯片上，配备 32MB Flash 和 8MB PSRAM。

```
┌──────────────────────────────────────────────────────────────┐
│                      XiaoClaw Firmware                       │
├──────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐      ┌─────────────────────────────┐    │
│  │   Voice I/O     │      │      Agent Brain            │    │
│  │   (xiaozhi)     │      │      (mimiclaw)             │    │
│  ├─────────────────┤      ├─────────────────────────────┤    │
│  │ • Wake word     │      │ • LLM API (Claude/GPT)      │    │
│  │ • ASR (server)  │────▶│ • Tool calling (ReAct)      │    │
│  │ • TTS playback  │◀────│ • Long-term memory          │    │
│  │ • Display/LCD   │      │ • Session management        │    │
│  │ • WiFi/Network  │      │ • Cron scheduler            │    │
│  └─────────────────┘      └─────────────────────────────┘    │
│           │                          ▲                       │
│           └──────── Bridge Layer ────┘                       │
└──────────────────────────────────────────────────────────────┘
```

## 功能特性

### 语音 I/O 层 (xiaozhi)

- 离线语音唤醒 ([ESP-SR](https://github.com/espressif/esp-sr))
- 通过服务器连接实现流式 ASR + TTS
- OPUS 音频编解码
- OLED / LCD 显示屏，支持表情显示
- 电池和电源管理
- 多语言支持（中文、英文、日文）
- WebSocket / MQTT 协议支持

### Agent 大脑层 (mimiclaw)

- LLM API 集成 (Anthropic Claude / OpenAI GPT)
- ReAct Agent 循环与工具调用
- 长期记忆 (基于 SPIFFS)
- 会话管理与对话历史
- 定时任务调度器
- 网络搜索能力 (Tavily / Brave)

## 硬件要求

- **ESP32-S3** 开发板
- **32MB Flash**（最低 16MB）
- **8MB PSRAM**（推荐 8线 PSRAM）
- 音频编解码器（带麦克风和扬声器）
- 可选：LCD/OLED 显示屏

### 支持的开发板

XiaoClaw 继承了 xiaozhi-esp32 的开发板支持，包括：

- ESP32-S3-BOX3
- M5Stack CoreS3 / AtomS3R
- 立创实战派 ESP32-S3 开发板
- LILYGO T-Circle-S3
- 以及 70+ 更多开发板...

## 快速开始

### 环境准备

- ESP-IDF v5.5 或更高版本
- Python 3.10+
- CMake 3.16+

### 编译

```bash
# 克隆仓库
git clone https://github.com/your-repo/xiaoclaw.git
cd xiaoclaw

# 设置目标芯片
idf.py set-target esp32s3

# 配置（可选）
idf.py menuconfig

# 编译
idf.py build
```

### 烧录

```bash
# 烧录并监控
idf.py -p PORT flash monitor
```

### 配置

从示例创建 `main/mimi/mimi_secrets.h`：

```c
#define MIMI_SECRET_WIFI_SSID       "你的WiFi名称"
#define MIMI_SECRET_WIFI_PASS       "你的WiFi密码"
#define MIMI_SECRET_API_KEY         "sk-ant-api03-xxxxx"
#define MIMI_SECRET_MODEL_PROVIDER  "anthropic"  // 或 "openai"
```

## 架构

### Bridge 层

Bridge 层连接语音 I/O 层与 Agent 大脑：

```
用户语音 → 唤醒词 → ASR (服务器) → 文本
                                     ↓
                               Bridge 层
                                     ↓
                               Agent 循环 (LLM)
                                     ↓
                               Bridge 层
                                     ↓
                               TTS 播放 → 扬声器
```

### 内存布局

| 分区   | 大小  | 用途             |
| ------ | ----- | ---------------- |
| ota_0  | 4MB   | 主固件           |
| ota_1  | 4MB   | OTA 备份         |
| spiffs | ~27MB | 记忆、会话、技能 |

### 任务布局

| 任务       | 核心 | 优先级 | 功能        |
| ---------- | ---- | ------ | ----------- |
| audio\_\*  | 0    | 8      | 音频 I/O    |
| main_loop  | 0    | 5      | 应用主循环  |
| bridge     | 0    | 5      | Bridge 通信 |
| agent_loop | 1    | 6      | LLM 处理    |

## 工具

Agent 可以使用多种工具：

| 工具               | 描述                 |
| ------------------ | -------------------- |
| `web_search`       | 搜索网络获取最新信息 |
| `get_current_time` | 获取当前日期/时间    |
| `gpio_write`       | 控制 GPIO 引脚       |
| `gpio_read`        | 读取 GPIO 状态       |
| `cron_add`         | 创建定时任务         |
| `cron_list`        | 列出定时任务         |
| `cron_remove`      | 删除定时任务         |
| `read_file`        | 从 SPIFFS 读取文件   |
| `write_file`       | 写入文件到 SPIFFS    |

## 记忆系统

XiaoClaw 在 SPIFFS 上以纯文本文件存储数据：

| 文件               | 用途           |
| ------------------ | -------------- |
| `SOUL.md`          | AI 人格定义    |
| `USER.md`          | 用户信息和偏好 |
| `MEMORY.md`        | 长期记忆       |
| `HEARTBEAT.md`     | 自主任务列表   |
| `cron.json`        | 定时任务       |
| `sessions/*.jsonl` | 对话历史       |

## 开发

### 项目结构

```
xiaoclaw/
├── main/
│   ├── bridge/           # Bridge 层（新增）
│   │   ├── bridge.h
│   │   └── bridge.cc
│   ├── mimi/             # Agent 大脑（来自 mimiclaw）
│   │   ├── agent/
│   │   ├── llm/
│   │   ├── tools/
│   │   ├── memory/
│   │   └── ...
│   ├── audio/            # 语音 I/O（来自 xiaozhi）
│   ├── protocols/
│   ├── boards/
│   ├── display/
│   └── application.cc    # 主应用
├── spiffs_data/          # SPIFFS 内容
├── CMakeLists.txt
└── sdkconfig.defaults.esp32s3
```

### 调试

使用串口 CLI 命令（通过 UART 端口）：

```
mimi> heap_info          # 内存状态
mimi> memory_read        # 查看长期记忆
mimi> session_list       # 列出对话
mimi> config_show        # 显示配置
```

## 相关项目

XiaoClaw 基于以下优秀项目构建：

- [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) — 语音交互框架
- [mimiclaw](https://github.com/memovai/mimiclaw) — ESP32 AI Agent

## 许可证

MIT License

## 致谢

- xiaozhi-esp32 团队的语音交互框架
- mimiclaw 团队的嵌入式 AI Agent 架构
- 乐鑫的 ESP-IDF 和 ESP-SR
