# MCP 远程工具一等公民方案

> 本文档描述如何将 MCP 远程工具提升为 Agent 的一等公民，实现自动连接和原子化工具注册。

## 现状分析

### 当前实现（包装器模式）

```
LLM 看到：
├── mcp_connect        ← 本地工具（手动调用）
├── mcp_disconnect     ← 本地工具（手动调用）
├── mcp_server.tools_list  ← 本地包装器
└── mcp_server.tools_call ← 本地包装器

实际远程工具（如 hue.set_color）：
  → 需要通过 mcp_server.tools_call({"name": "hue.set_color", "arguments": {...}})
  → LLM 看不到真实工具名和 schema
```

### 问题

| 问题 | 影响 |
|------|------|
| LLM 看不到真实工具 | 无法理解远程工具能力 |
| 两步操作 | 先 list 再 call，增加延迟 |
| 工具 schema 丢失 | 无法验证参数 |
| 非原子化 | 泛型调用，无法精确控制 |

### 初始化流程（当前）

```
mimiclaw_init()
  └─> tool_mcp_client_init()
          └─> 只注册 4 个包装器，不连接任何服务器
```

---

## 目标架构

### 目标流程

```
mimiclaw_init()
  └─> tool_mcp_client_init()
          ├─> 解析 mcp-servers/SKILL.md 获取服务器列表
          ├─> auto_connect=true 的服务器自动连接
          │     ├─> 调用 tools/list 获取远程工具
          │     └─> 将每个远程工具注册为独立工具
          └─> LLM 看到完整的工具列表
```

### LLM 视角（目标）

```
LLM 看到：
├── read_file, write_file, gpio_write, ... ← 本地原子工具
├── hue.set_color  ← 远程 MCP 工具（直接注册）
├── hue.set_brightness
├── weather.get_forecast
└── mcp_disconnect ← 断开连接（仍保留）
```

---

## 实现方案

### 1. SKILL.md 配置扩展

在 `spiffs_data/skills/mcp-servers/SKILL.md` 中添加 `auto_connect` 字段：

```yaml
## test_server
- host: 192.168.1.3
- port: 8080
- endpoint: mcp_server
- auto_connect: true   # 新增：启动时自动连接
- default_params:
  - tenant_id: 10542
```

### 2. 新增 API

```c
// tool_mcp_client.h

/**
 * 解析 SKILL.md 获取所有服务器配置
 * @param configs 输出配置数组
 * @param max_count 最大数量
 * @param count 实际数量
 * @return ESP_OK on success
 */
esp_err_t mcp_load_all_server_configs(mcp_server_config_t configs[],
                                      int max_count, int *count);

/**
 * 连接服务器并注册其远程工具为一等公民
 * @param server_name 服务器名称
 * @return ESP_OK on success
 */
esp_err_t mcp_connect_and_register(const char *server_name);
```

### 3. 工具注册表增强

在 `tool_registry.h` 中添加移除功能：

```c
/**
 * 从注册表移除工具
 * @param name 工具名称
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found
 */
esp_err_t tool_registry_remove(const char *name);

/**
 * 清除所有动态注册的工具（用于断开连接时）
 */
void tool_registry_clear_dynamic(void);
```

### 4. 连接流程

```c
esp_err_t mcp_connect_and_register(const char *server_name)
{
    // 1. 获取服务器配置
    mcp_server_config_t config;
    err = skill_loader_get_mcp_server_config(server_name, ...);
    if (err != ESP_OK) return err;

    // 2. 连接 MCP 服务器
    err = mcp_do_connect(server_name, &config);
    if (err != ESP_OK) return err;

    // 3. 调用 tools/list 获取所有远程工具
    mcp_tool_info_t tools[64];
    int tool_count;
    err = mcp_list_tools(tools, &tool_count);
    if (err != ESP_OK) return err;

    // 4. 将每个工具注册为独立工具
    for (int i = 0; i < tool_count; i++) {
        mimi_tool_t tool = {
            .name = tools[i].name,           // 如 "hue.set_color"
            .description = tools[i].desc,
            .input_schema_json = tools[i].schema,
            .execute = mcp_remote_execute,   // 通用远程执行
            .concurrency_safe = false,
        };
        tool_registry_add(&tool);
    }

    tool_registry_rebuild_json();
    return ESP_OK;
}
```

### 5. 初始化流程

```c
// mimi.c 或 tool_mcp_client.c
esp_err_t tool_mcp_client_init(void)
{
    // 注册本地工具（mcp_connect, mcp_disconnect 等）
    register_local_tools();

    // 解析所有服务器配置
    mcp_server_config_t configs[8];
    int count;
    mcp_load_all_server_configs(configs, 8, &count);

    // 自动连接标记为 auto_connect=true 的服务器
    for (int i = 0; i < count; i++) {
        if (configs[i].auto_connect) {
            ESP_LOGI(TAG, "Auto-connecting to MCP server: %s", configs[i].name);
            mcp_connect_and_register(configs[i].name);
        }
    }

    return ESP_OK;
}
```

---

## 文件修改清单

| 文件 | 修改内容 |
|------|----------|
| `main/mimi/tools/tool_mcp_client.h` | 添加 `mcp_load_all_server_configs()`, `mcp_connect_and_register()` |
| `main/mimi/tools/tool_mcp_client.c` | 实现上述函数，连接后自动注册工具 |
| `main/mimi/tools/tool_registry.h` | 添加 `tool_registry_remove()`, `tool_registry_clear_dynamic()` |
| `main/mimi/tools/tool_registry.c` | 实现移除功能 |
| `spiffs_data/skills/mcp-servers/SKILL.md` | 添加 `auto_connect` 字段 |
| `main/mimi/mimi.c` | 初始化时自动连接 MCP |

---

## 默认参数注入

当前实现中，`mcp-servers/SKILL.md` 的 `default_params` 会在调用时自动注入。新的实现需要保留此功能：

```c
// mcp_remote_execute 中
static const char* s_default_params = NULL;  // 从 SKILL.md 解析

esp_err_t mcp_remote_execute(const char *input_json, char *output, size_t output_size)
{
    // 1. 解析 input_json
    // 2. 合并 s_default_params（如 tenant_id）
    // 3. 发送到远程服务器
    // 4. 返回结果
}
```

---

## 后续优化

### 与技能结晶化结合

结晶化后的技能可以包含 MCP 工具调用：

```lua
-- /spiffs/skills/auto/auto_light_scene/impl.lua

local mcp = require("mcp")

-- 设置客厅灯为暖白色
mcp.call("hue.set_color", { room = "living", color = "warm_white" })
mcp.call("hue.set_brightness", { room = "living", level = 80 })

-- 延迟
lua.run("time.sleep(1)")

-- 设置卧室灯为蓝色
mcp.call("hue.set_color", { room = "bedroom", color = "blue" })

return "Light scene activated"
```

---

## 测试验证

1. **启动日志检查**
   ```
   I (1234) MCP: Auto-connecting to MCP server: test_server
   I (1235) MCP: Registered 5 remote tools: hue.set_color, hue.set_brightness, ...
   ```

2. **工具列表验证**
   - 调用 `mcp_server.tools_list` 应显示所有远程工具
   - LLM 应能直接调用 `hue.set_color` 而无需包装器

3. **断开重连**
   - 调用 `mcp_disconnect` 应移除所有远程工具
   - 重新连接应重新注册
