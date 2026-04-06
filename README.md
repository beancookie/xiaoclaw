# XiaoClaw: AI Voice Assistant with Local Agent Brain

<p align="center">
  <strong>ESP32-S3 AI Voice Assistant — Voice I/O + Local LLM Agent</strong>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
  <a href="https://github.com/anthropics/claude-code"><img src="https://img.shields.io/badge/Claude-Opus_4.6-blue.svg" alt="Claude"></a>
</p>

---

## Introduction

**XiaoClaw** is a unified ESP32-S3 firmware that combines voice interaction with a local AI agent brain. It integrates:

- **xiaozhi-esp32** — Voice I/O layer: audio recording, playback, wake word detection, display, and network communication
- **mimiclaw** — Agent brain: LLM-powered reasoning, tool calling, memory management, and autonomous task execution

All running on a single ESP32-S3 chip with 32MB Flash and 8MB PSRAM.

```mermaid
graph TB
    subgraph Firmware["XiaoClaw Firmware"]
        subgraph VoiceIO["Voice I/O (xiaozhi)"]
            A[("Wake Word")]
            B[("ASR (Server)")]
            C[("TTS Playback")]
            D[("Display/LCD")]
            E[("WiFi/Network")]
        end

        subgraph Agent["Agent Brain (mimiclaw)"]
            F["LLM API (Claude/GPT)"]
            G["Tool Calling (ReAct)"]
            H["Long-term Memory"]
            I["Session Management"]
            J["Cron Scheduler"]
            K["Web Search"]
        end

        VoiceIO <-->|"Bridge Layer"| Agent
    end

    style VoiceIO fill:#e1f5fe,stroke:#01579b
    style Agent fill:#f3e5f5,stroke:#4a148c
    style Firmware fill:#fafafa,stroke:#424242
```

## Features

### Voice I/O Layer (xiaozhi)
- Offline wake word detection ([ESP-SR](https://github.com/espressif/esp-sr))
- Streaming ASR + TTS via server connection
- OPUS audio codec
- OLED / LCD display with emoji support
- Battery and power management
- Multi-language support (Chinese, English, Japanese)
- WebSocket / MQTT protocol support

### Agent Brain Layer (mimiclaw)
- LLM API integration (Anthropic Claude / OpenAI GPT)
- Modular ReAct agent loop with `AgentRunner` execution engine
- Hook system for iteration/tool callbacks (`before_iteration`, `after_iteration`, `on_tool_result`, `before_tool_execute`)
- Checkpoint system for crash recovery
- Context Builder with modular system prompt construction
- Session consolidation with automatic history compression
- Long-term memory (SPIFFS-based)
- Session management with cursor-based history tracking
- Cron scheduler for autonomous tasks
- Web search capability (Tavily / Brave)

## Hardware Requirements

- **ESP32-S3** development board
- **32MB Flash** (minimum 16MB)
- **8MB PSRAM** (Octal PSRAM recommended)
- Audio codec with microphone and speaker
- Optional: LCD/OLED display

### Supported Boards

XiaoClaw inherits board support from xiaozhi-esp32, including:
- ESP32-S3-BOX3
- M5Stack CoreS3 / AtomS3R
- LiChuang ESP32-S3 Development Board
- LILYGO T-Circle-S3
- And 70+ more boards...

## Quick Start

### Prerequisites

- ESP-IDF v5.5 or later
- Python 3.10+
- CMake 3.16+

### Build

```bash
# Clone the repository
git clone https://github.com/your-repo/xiaoclaw.git
cd xiaoclaw

# Set target
idf.py set-target esp32s3

# Configure (optional)
idf.py menuconfig

# Build
idf.py build
```

### Flash

```bash
# Flash and monitor
idf.py -p PORT flash monitor

# Flash app only (skip SPIFFS to preserve data)
esptool.py -p PORT write_flash 0x20000 ./build/xiaozhi.bin
```

### Configuration

Create `main/mimi/mimi_secrets.h` from the example:

```c
#define MIMI_SECRET_WIFI_SSID       "YourWiFiName"
#define MIMI_SECRET_WIFI_PASS       "YourWiFiPassword"
#define MIMI_SECRET_API_KEY         "sk-ant-api03-xxxxx"
#define MIMI_SECRET_MODEL_PROVIDER  "anthropic"  // or "openai"
```

## Architecture

### Bridge Layer

The bridge layer connects the voice I/O layer with the agent brain:

```mermaid
flowchart LR
    A["User Voice"] --> B["Wake Word"]
    B --> C["ASR (Server)"]
    C --> D["Text"]
    D --> E["Bridge Layer"]
    E --> F["Agent Loop (LLM)"]
    F --> G["Bridge Layer"]
    G --> H["TTS Playback"]
    H --> I["Speaker"]

    style A fill:#e1f5fe
    style B fill:#e1f5fe
    style C fill:#e1f5fe
    style D fill:#e1f5fe
    style I fill:#e1f5fe
    style E fill:#fff3e0
    style G fill:#fff3e0
    style F fill:#f3e5f5
```

### Memory Layout

| Partition | Size | Purpose |
|-----------|------|---------|
| ota_0 | 4MB | Main firmware |
| ota_1 | 4MB | OTA backup |
| spiffs | ~27MB | Memory, sessions, skills |

### Task Layout

| Task | Core | Priority | Function |
|------|------|----------|----------|
| audio_* | 0 | 8 | Audio I/O |
| main_loop | 0 | 5 | Application main |
| bridge | 0 | 5 | Bridge communication |
| agent_loop | 1 | 6 | LLM processing |

## Tools

The agent can use various tools:

| Tool | Description |
|------|-------------|
| `web_search` | Search the web for current information |
| `get_current_time` | Get current date/time |
| `gpio_write` | Control GPIO pins |
| `gpio_read` | Read GPIO state |
| `gpio_read_all` | Read all allowed GPIO pins |
| `lua_eval` | Execute a Lua code string directly |
| `lua_run` | Execute a Lua script from SPIFFS |
| `mcp_connect` | Connect to an MCP server |
| `mcp_disconnect` | Disconnect from MCP server |
| `cron_add` | Schedule a task |
| `cron_list` | List scheduled tasks |
| `cron_remove` | Remove a scheduled task |
| `read_file` | Read file from SPIFFS |
| `write_file` | Write file to SPIFFS |
| `edit_file` | Edit file (find-and-replace) |
| `list_dir` | List files in directory |

**Note:** GPIO tools respect board-specific policies defined in `gpio_policy.h`.

### MCP Client (Dynamic Remote Tools)

XiaoClaw supports connecting to remote MCP servers to dynamically discover and call tools. Server configurations are stored in `mcp-servers.md` skill file.

```mermaid
flowchart TB
    subgraph Config["Configuration"]
        A["mcp-servers.md<br/>skill file"] --> B["List of available<br/>MCP servers"]
    end

    subgraph Init["tool_mcp_client_init()"]
        C["Register mcp_connect<br/>and mcp_disconnect tools"]
    end

    subgraph Connect["LLM calls mcp_connect"]
        D["skill_loader_get_mcp_server_config()"] --> E["esp_mcp_create()"]
        E --> F["esp_mcp_mgr_init()"]
        F --> G["mcp_initialize()"]
        G --> H["mcp_list_tools()"]
        H --> I["tool_registry_add() × N"]
        I --> J["tool_registry_rebuild_json()"]
    end

    subgraph LLM_Call["Remote Tool Calling"]
        K["LLM requests tools"] --> L["mcp.server_name.tool"]
        L --> M["mcp_tool_execute()"]
        M --> N["esp_mcp_mgr_post()"]
        N --> O["Wait for response"]
        O --> P["Return JSON result to LLM"]
    end

    style Config fill:#e3f2fd,stroke:#1565c0
    style Init fill:#e8f5e9,stroke:#2e7d32
    style Connect fill:#fff3e0,stroke:#ef6c00
    style LLM_Call fill:#f3e5f5,stroke:#7b1fa2
```

**Configuration file:** `/spiffs/skills/mcp-servers.md`
```markdown
# MCP Servers

## my_server
- host: 192.168.1.100
- port: 8000
- endpoint: mcp
```

**Available tools:**
| Tool | Description |
|------|-------------|
| `mcp_connect` | Connect to an MCP server by name |
| `mcp_disconnect` | Disconnect from current server |

**Python MCP Server Example:** `scripts/mcp_server.py`
```bash
pip install "mcp[cli]"
python scripts/mcp_server.py --port 8000
```

Remote tools are registered with the `{server_name}.` prefix (e.g., `my_server.get_device_status`), distinguishing them from local tools.

## Memory System

XiaoClaw stores data in plain text files on SPIFFS with session consolidation support:

| Path | Purpose |
|------|---------|
| `/spiffs/SOUL.md` | AI personality definition |
| `/spiffs/USER.md` | User information and preferences |
| `/spiffs/MEMORY.md` | Long-term memory |
| `/spiffs/HEARTBEAT.md` | Autonomous task list |
| `/spiffs/cron.json` | Scheduled jobs |
| `/spiffs/sessions/tg_*.jsonl` | Conversation history (JSONL format) |
| `/spiffs/sessions/tg_*.meta` | Session metadata (cursor, consolidated count) |
| `/spiffs/archive/tg_*.archive` | Archived old messages |

### Session Management

- **Cursor-based tracking**: Each session tracks read position via cursor
- **Consolidation**: When session exceeds `max_history` messages, oldest messages are archived
- **LRU cache**: Active sessions cached in memory for fast access
- **Checkpoint recovery**: Agent can resume from last checkpoint on crash

### Skills System

Skills are loaded from `/spiffs/skills/` directory with YAML frontmatter support:

```yaml
---
name: weather
description: Get current weather information
always: false
---
# Weather Skill
Use the `weather` tool to...
```

- **`always: true`**: Skill content always injected into system prompt
- **`requires.bins`**: CLI tools required by the skill
- **`requires.env`**: Environment variables needed

## Development

### Project Structure

```
xiaoclaw/
├── main/
│   ├── mimi/             # Agent brain (from mimiclaw)
│   │   ├── agent/        # Agent loop, runner, hooks, checkpoint
│   │   │   ├── agent_loop.c   # Main agent task loop
│   │   │   ├── runner.c       # ReAct execution engine
│   │   │   ├── context_builder.c # System prompt construction
│   │   │   ├── hook.c         # Agent hooks implementation
│   │   │   └── checkpoint.c   # Crash recovery checkpoint
│   │   ├── bus/          # Message bus
│   │   ├── channels/     # Telegram, Feishu bot integrations
│   │   ├── cli/          # Serial CLI
│   │   ├── cron/         # Cron scheduler service
│   │   ├── gateway/      # WebSocket server
│   │   ├── heartbeat/    # Autonomous task heartbeat
│   │   ├── llm/          # LLM proxy
│   │   ├── memory/       # Memory store, session manager, consolidator
│   │   │   ├── memory_store.c    # Long-term memory
│   │   │   ├── session_manager.c # Session with cursor/consolidation
│   │   │   └── consolidator.c     # Automatic history compression
│   │   ├── onboard/      # WiFi onboarding
│   │   ├── ota/          # OTA updates
│   │   ├── proxy/        # HTTP proxy
│   │   ├── skills/       # Skill loader with frontmatter
│   │   ├── tools/        # Tool registry with concurrency support
│   │   └── wifi/         # WiFi manager
│   ├── audio/            # Voice I/O (from xiaozhi)
│   ├── bridge/           # Bridge layer
│   ├── display/
│   ├── protocols/
│   ├── boards/
│   ├── assets.cc/h       # Assets management
│   ├── application.cc/h  # Main application
│   ├── device_state.h   # Device state
│   ├── device_state_machine.cc/h # State machine
│   ├── idf_component.yml # Component manifest
│   ├── main.cc           # Entry point
│   ├── mcp_server.cc/h   # MCP server
│   ├── ota.cc/h          # OTA updates
│   ├── settings.cc/h     # Settings management
│   └── system_info.cc/h  # System info
├── spiffs_data/          # SPIFFS content
├── CMakeLists.txt
└── sdkconfig.defaults.esp32s3
```

### Debugging

Use serial CLI commands (via UART port):

```
mimi> heap_info          # Memory status
mimi> memory_read        # View long-term memory
mimi> session_list       # List conversations
mimi> config_show        # Show configuration
```

## Related Projects

XiaoClaw is built upon these excellent projects:

- [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) — Voice interaction framework
- [mimiclaw](https://github.com/memovai/mimiclaw) — ESP32 AI agent

## License

MIT License

## Acknowledgments

- xiaozhi-esp32 team for the voice interaction framework
- mimiclaw team for the embedded AI agent architecture
- Espressif for ESP-IDF and ESP-SR
