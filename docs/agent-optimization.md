# mimi Agent 优化详细方案

> 基于 GenericAgent 与 Hermes Agent 核心设计，结合 XiaoClaw ESP32-S3 硬件约束。
> 本文档为详细实现指南，替代之前的高层概述。

---

## 一、设计哲学

| 框架 | 核心思想 | 移植要点 |
|------|---------|---------|
| **GenericAgent** | 原子工具 + 自进化（技能结晶化） | 不预设技能，运行时组合；成功后将工具调用序列保存为 SOP |
| **Hermes Agent** | 闭环学习（复盘）+ 技能策展 + 分层记忆 | 任务后评估成败，更新 skill metadata；按热度选择性加载技能 |

**改造目标**：从"静态脚本执行器"进化为"能在使用中生长技能的嵌入式 Agent"。

---

## 二、记忆层级（L0-L4）详细设计

### 2.1 各层职责

```
L0: SOUL.md (硬编码系统约束 - 无需改动)
    │
L1: /spiffs/memory/skill_index.json (技能索引 - JSON，≤2KB)
    │
L2: /spiffs/memory/facts.json (用户偏好/环境事实)
    │
L3: /spiffs/skills/auto/*.md (自动生成的 SOP，HOT 技能)
    │
L4: /spiffs/sessions/archive/ (压缩后的历史会话)
```

### 2.2 L0 元规则（SOUL.md）

系统约束硬编码，如 Lua 脚本超时时间、禁止危险操作等。不做修改。

### 2.3 L1 技能索引层

**文件**：`/spiffs/memory/skill_index.json`

**结构**（JSON）：
```json
{
  "skills": [
    {
      "name": "mcp-servers",
      "path": "/spiffs/skills/mcp-servers/SKILL.md",
      "is_auto": false,
      "usage_count": 12,
      "success_count": 10,
      "success_rate": 0.83,
      "last_used": 1745270400,
      "is_hot": true
    },
    {
      "name": "auto_light_control_3a7f",
      "path": "/spiffs/skills/auto/auto_light_control_3a7f.md",
      "is_auto": true,
      "usage_count": 5,
      "success_count": 5,
      "success_rate": 1.0,
      "last_used": 1745268000,
      "is_hot": true
    }
  ],
  "hot_threshold": 3,
  "last_updated": 1745271000
}
```

**约束**：严格 ≤30 行，约 2KB。不得写入详细信息，只做导航。

### 2.4 L2 事实层

**文件**：`/spiffs/memory/facts.json`（可选，亦用 USER.md）

**原则**（来自 GenericAgent 的 memory_management_sop.md）：
- 只存储**行动验证成功**的信息
- 禁止存储猜测、临时变量、未验证假设
- 按 `## [SECTION]` 组织

### 2.5 L3 技能层（自动生成 SOP）

**目录**：`/spiffs/skills/auto/`

**格式**（SKILL.md with YAML frontmatter）：
```yaml
---
name: auto_light_control_3a7f
description: Auto-generated skill for: 控制客厅灯光开关
always: false
auto: true
created_from: lua_run("light_on.lua") -> lua_run("light_off.lua")
step_count: 2
success_rate: 1.0
---
# Auto Skill: light_control

## Intent
控制客厅灯光开关

## Trigger
用户说"开灯"、"关灯"、"客厅灯"

## Tool Sequence
1. lua_run(script="light_on.lua")  # 开灯脚本
2. lua_run(script="light_off.lua")  # 关灯脚本

## Pitfalls
- 灯光控制脚本位于 /spiffs/lua/ 目录
- 确认脚本存在后再调用
```

### 2.6 L4 归档层

**目录**：`/spiffs/sessions/archive/`（已存在）

**策略**：会话压缩归档，仅保留关键路径。

---

## 三、技能元数据系统（skill_meta）

### 3.1 数据结构

**新增文件**：`main/mimi/skills/skill_meta.h`

```c
typedef struct {
    char name[64];            // 技能名
    char path[256];           // 完整路径
    bool is_auto;             // 是否自动生成
    int usage_count;          // 使用次数
    int success_count;        // 成功次数
    float success_rate;       // 计算得出
    time_t last_used;         // 上次使用时间戳
    bool is_hot;              // usage_count >= HOT_THRESHOLD
} skill_meta_t;
```

**常量**：
```c
#define SKILL_META_HOT_THRESHOLD 3   // 达到此门槛视为"热"技能
#define SKILL_INDEX_PATH "/spiffs/memory/skill_index.json"
```

### 3.2 API 设计

**新增文件**：`main/mimi/skills/skill_meta.c`

| 函数 | 功能 |
|------|------|
| `skill_meta_init()` | 启动时加载 skill_index.json |
| `skill_meta_get(name, meta)` | 获取指定技能的元数据 |
| `skill_meta_record_usage(name, success)` | 记录一次使用（更新 usage_count, success_count） |
| `skill_meta_update(name, meta)` | 允许 Agent 修改元数据（如 description） |
| `skill_meta_get_all_json(buf, size)` | 获取 L1 索引 JSON（给 context_builder 用） |
| `skill_meta_get_hot_skills(buf, max)` | 获取热门技能列表（usage_count >= 3） |
| `skill_meta_save()` | 将内存中的元数据持久化到 SPIFFS |

### 3.3 与 skill_loader.c 的集成

**修改** `skill_loader.c`：
- `skill_info_t` 结构体新增字段：`usage_count`, `success_rate`, `last_used`
- 扫描技能目录时，合并 skill_index.json 中的元数据
- 新增 `skill_loader_get_hot_skills()` 函数

---

## 四、技能结晶化（skill_crystallize）

### 4.1 触发条件

```
task_success == true
AND step_count > 1
AND (is_repetitive OR step_count > 3)
AND skill_not_exists(similar_name)
```

### 4.2 数据结构

**新增文件**：`main/mimi/skills/skill_crystallize.h`

```c
typedef struct {
    bool last_task_success;
    int step_count;
    bool is_repetitive;
    const char *user_intent;
    const char *tool_sequence_json;  // JSON array of {tool, input}
    int sequence_len;
} crystallize_context_t;
```

### 4.3 API 设计

**新增文件**：`main/mimi/skills/skill_crystallize.c`

| 函数 | 功能 |
|------|------|
| `skill_crystallize_if_needed(ctx)` | 主入口，条件判断 + 创建 |
| `skill_crystallize_should_create(ctx)` | 判断是否满足结晶条件 |
| `skill_crystallize_create(name, intent, tool_seq)` | 创建 auto_*.md 文件 |
| `skill_crystallize_generate_name(intent, buf, size)` | 从 user_intent 生成唯一名称 |

### 4.4 结晶化流程

```
1. 任务成功后，agent_loop 调用 skill_crystallize_if_needed()
2. 检查条件（success + step_count + 重复检测）
3. 生成技能名：auto_{intent_hash}_{timestamp}.md
4. 写入 /spiffs/skills/auto/auto_xxx/SKILL.md
5. 更新 skill_index.json（新增条目，is_auto=true）
```

### 4.5 生成技能名算法

```c
void skill_crystallize_generate_name(const char *intent, char *buf, size_t size) {
    // 取 intent 前3个词，hash取前4位，附时间戳后4位
    // 例: "控制客厅灯光" -> "auto_light_ctrl_a3f2_7d2e"
}
```

---

## 五、闭环学习钩子（learning_hooks）

### 5.1 Hook 接口

**修改文件**：`main/mimi/agent/hook.h`

扩展 `AgentHooks` 结构体：

```c
typedef struct {
    void (*before_iteration)(int iteration);
    void (*after_iteration)(int iteration, const char *final_content);
    void (*before_tool_execute)(const char *tool_name);
    void (*after_tool_execute)(const char *tool_name, const char *result, bool success);

    // 新增：闭环学习钩子
    bool (*evaluate_task_result)(const char *final_output,
                                  const char *tool_sequence_json);
    void (*on_task_end)(const char *chat_id, const AgentRunResult *result);
} AgentHooks;
```

### 5.2 默认实现

**新增文件**：`main/mimi/agent/learning_hooks.c`

```c
// 评估任务是否成功
bool learning_hook_evaluate(const char *final_output,
                             const char *tool_sequence_json) {
    // 成功条件：
    // - final_output 非空
    // - 不包含失败关键词（error, failed, sorry, cannot, unable）
    // - tool_sequence_json 非空（至少调用了工具）
}

// 任务结束处理
void learning_hook_on_task_end(const char *chat_id,
                                const AgentRunResult *result) {
    if (!result) return;

    // 1. 更新每个工具的元数据
    for (int i = 0; i < result->tools_used_count; i++) {
        skill_meta_record_usage(result->tools_used[i], result->task_success);
    }

    // 2. 检查是否需要结晶化
    if (result->task_success && result->tool_sequence_len > 1) {
        crystallize_context_t ctx = {
            .last_task_success = true,
            .step_count = result->tool_sequence_len,
            .tool_sequence_json = result->tool_sequence_json,
        };
        skill_crystallize_if_needed(&ctx);
    }
}
```

---

## 六、AgentRunResult 扩展

### 6.1 修改文件

`main/mimi/agent/runner.h` - 扩展 `AgentRunResult`：

```c
typedef struct {
    char *final_content;
    cJSON *messages;
    int tools_used_count;
    char tools_used[32][32];        // 工具名列表
    int usage_prompt_tokens;
    int usage_completion_tokens;
    const char *stop_reason;
    char *error;

    // 新增：闭环学习支持
    bool task_success;               // 任务是否成功
    char tool_sequence_json[2048];  // JSON数组: [{"tool":"gpio_write","input":"..."}]
    int tool_sequence_len;           // 工具调用次数
} AgentRunResult;
```

### 6.2 tool_sequence_json 格式

```json
[{"tool":"lua_run","input":"{\"script\":\"light_on.lua\"}"},{"tool":"read_file","input":"..."}]
```

---

## 七、上下文构建器改造（context_builder.c）

### 7.1 修改目标

**原则**：不再全量加载技能，只加载 L1 索引 + 热门的 L3 技能。

**修改函数**：`append_skills_section()`

**改动前**：
- 加载所有技能列表（浪费 token）

**改动后**：
```c
static size_t append_skills_section(char *buf, size_t size, size_t offset) {
    size_t off = 0;

    // L1: 技能索引（始终加载）
    char l1_index[2048];
    size_t l1_len = skill_meta_get_all_json(l1_index, sizeof(l1_index));
    if (l1_len > 0) {
        off += snprintf(buf + offset + off, size - offset - off,
            "## Skill Index (L1)\n\n%s\n\n", l1_index);
    }

    // L3: 热门自动技能（只加载 is_hot == true 的）
    char l3_hot[4096];
    size_t l3_len = skill_meta_get_hot_skills(l3_hot, sizeof(l3_hot));
    if (l3_len > 0) {
        off += snprintf(buf + offset + off, size - offset - off,
            "## Active Auto-Skills (L3)\n\n%s\n\n", l3_hot);
    }

    // Always 技能（保持原有逻辑）
    char always_content[8192];
    size_t always_len = skill_loader_get_always_content(always_content,
                                                         sizeof(always_content));
    if (always_len > 0) {
        off += snprintf(buf + offset + off, size - offset - off,
            "## Always-Active Skills\n\n%s\n\n", always_content);
    }

    return off;
}
```

### 7.2 Token 预算（16KB context buffer）

| 组件 | 估算大小 |
|------|---------|
| Identity | ~500 bytes |
| Tools section | ~1KB |
| **L1 Skill index** | ~800 bytes (JSON, ≤32技能) |
| **L3 Hot skills** | ~1-3KB (仅热门技能) |
| Always skills | ~2-4KB (mcp-servers, skill-creator 等) |
| Memory (MEMORY.md) | ~500 bytes |
| Recent daily notes | ~500 bytes |
| SOUL.md + USER.md | ~400 bytes |
| **总计** | ~6-10KB |
| Safety buffer | ~1KB |

---

## 八、Agent Loop 集成

### 8.1 修改文件

`main/mimi/agent/agent_loop.c`

**改动点 1**：构建 `AgentRunSpec` 时传入 hooks
```c
AgentRunSpec spec = {
    // ... 原有字段 ...
    .hooks = &learning_hooks_default,  // 新增
};
```

**改动点 2**：任务结束后调用闭环学习
```c
// 在 agent_runner_run() 返回后
if (result.error == NULL && result.final_content != NULL) {
    bool task_success = learning_hook_evaluate(result.final_content,
                                               result.tool_sequence_json);
    ((AgentRunResult*)&result)->task_success = task_success;

    learning_hook_on_task_end(msg.chat_id, &result);
}
```

### 8.2 Runner 修改

`main/mimi/agent/runner.c` - 在 ReAct 循环中构建 tool_sequence_json：

```c
// 每次工具执行后，追加到 tool_sequence_json
for (int i = 0; i < resp.call_count && tools_used_idx < 32; i++) {
    // 追加到 result->tool_sequence_json
    if (tools_used_idx == 0) {
        snprintf(result->tool_sequence_json,
                 sizeof(result->tool_sequence_json),
                 "[{\"tool\":\"%s\",\"input\":%s}",
                 resp.calls[i].name, resp.calls[i].input);
    } else {
        char entry[512];
        snprintf(entry, sizeof(entry),
                 ",{\"tool\":\"%s\",\"input\":%s}",
                 resp.calls[i].name, resp.calls[i].input);
        strncat(result->tool_sequence_json, entry,
                sizeof(result->tool_sequence_json) - strlen(result->tool_sequence_json) - 1);
    }
    tools_used_idx++;
}
// 循环结束后追加结束符
strncat(result->tool_sequence_json, "]",
        sizeof(result->tool_sequence_json) - strlen(result->tool_sequence_json) - 1);
result->tool_sequence_len = tools_used_idx;
```

---

## 九、初始化顺序（mimi.c）

```c
// mimiclaw_init() 中新增
ESP_ERROR_CHECK(skill_meta_init());         // 技能元数据系统
// skill_crystallize 不需要单独init，因为无内部状态
```

---

## 十、CMakeLists.txt 更新

**修改文件**：`main/mimi/CMakeLists.txt`

```cmake
set(MIMI_SRCS
    ...
    # 新增
    skills/skill_meta.c
    skills/skill_crystallize.c
    agent/learning_hooks.c
    memory/hierarchy.c
)
```

---

## 十一、完整文件变更清单

### 新建文件（8个）

| 文件路径 | 功能 |
|---------|------|
| `main/mimi/skills/skill_meta.h` | 技能元数据头文件 |
| `main/mimi/skills/skill_meta.c` | 技能元数据实现（JSON持久化） |
| `main/mimi/skills/skill_crystallize.h` | 技能结晶化头文件 |
| `main/mimi/skills/skill_crystallize.c` | 技能结晶化实现 |
| `main/mimi/agent/learning_hooks.h` | 闭环学习钩子头文件 |
| `main/mimi/agent/learning_hooks.c` | 闭环学习钩子默认实现 |
| `main/mimi/memory/hierarchy.h` | L0-L4 记忆层级头文件 |
| `main/mimi/memory/hierarchy.c` | L0-L4 记忆层级实现 |

### 修改文件（9个）

| 文件路径 | 改动 |
|---------|------|
| `main/mimi/skills/skill_loader.h` | `skill_info_t` 新增 `usage_count/success_rate/last_used` |
| `main/mimi/skills/skill_loader.c` | 合并 metadata，新增 `skill_loader_get_hot_skills()` |
| `main/mimi/agent/runner.h` | `AgentRunResult` 新增 `task_success/tool_sequence_json/tool_sequence_len` |
| `main/mimi/agent/runner.c` | ReAct 循环中构建 `tool_sequence_json` |
| `main/mimi/agent/hook.h` | `AgentHooks` 扩展 `evaluate_task_result/on_task_end` |
| `main/mimi/agent/agent_loop.c` | 传入 hooks，任务结束后调用 `learning_hook_on_task_end` |
| `main/mimi/agent/context_builder.c` | 重写 `append_skills_section()`，实现 L1/L3 选择性加载 |
| `main/mimi/memory/memory_store.c` | 新增 `memory_get_facts()` for L2 |
| `main/mimi/mimi.c` | 添加 `skill_meta_init()` 调用 |
| `main/mimi/CMakeLists.txt` | 添加新源文件 |

---

## 十二、验证方案

### 12.1 编译验证
```bash
idf.py build
```
确保所有新增文件编译通过，无报错。

### 12.2 技能元数据验证
1. 在 SPIFFS 中预先创建 `skill_index.json`：
```json
{"skills":[{"name":"test","path":"/spiffs/skills/test/SKILL.md","usage_count":5}],"hot_threshold":3}
```
2. 启动后检查日志，确认 `skill_meta_init()` 正确加载。

### 12.3 结晶化验证
1. 发起一个多步骤任务（如连续调用 lua_run）
2. 任务成功后检查 `/spiffs/skills/auto/` 目录是否生成新的 `auto_*.md` 文件
3. 检查 `skill_index.json` 是否更新

### 12.4 分层记忆验证
1. 创建多个自动技能，设置不同 `usage_count`
2. 触发 context_builder，观察是否只加载 `is_hot=true` 的技能
3. 对比修改前后 system prompt 长度

### 12.5 闭环学习验证
1. 发起一个会失败的任务（如脚本不存在）
2. 检查 `learning_hook_evaluate()` 是否正确返回 false
3. 检查 `success_count` 是否未增加

---

## 十三、关键参考代码

### GenericAgent 参考

**记忆管理 SOP** (`memory_management_sop.md`)：
- L0-L4 各层职责定义
- 行动验证原则："No Execution, No Memory"
- 信息分类决策树

**ga.py 中的 start_long_term_update 工具**：
- 触发长期记忆蒸馏的机制
- 从成功任务中提取环境事实和用户偏好

### Hermes Agent 参考

**skill_manager_tool.py**：
- 技能创建的完整实现（create/edit/patch/delete/write_file）
- YAML frontmatter 验证
- 安全扫描集成
- 原子写入（atomic write）模式

**agent/skill_utils.py**：
- `parse_frontmatter()` - YAML 解析
- `iter_skill_index_files()` - 技能目录遍历
- `skill_matches_platform()` - 平台匹配

**tools/registry.py**：
- 工具注册模式
- 工具发现机制

---

## 十四、实施顺序

```
Phase 1: 技能元数据系统
  ├─ 新建 skill_meta.h/c
  ├─ 修改 skill_loader.h/c
  └─ mimi.c 添加 skill_meta_init()

Phase 2: 闭环学习钩子
  ├─ 新建 learning_hooks.h/c
  ├─ 修改 hook.h（扩展 AgentHooks）
  ├─ 修改 runner.h/c（添加 task_success 和 tool_sequence_json）
  └─ 修改 agent_loop.c（调用 hooks）

Phase 3: 技能结晶化
  ├─ 新建 skill_crystallize.h/c
  └─ 在 learning_hook_on_task_end() 中调用

Phase 4: 分层记忆
  ├─ 新建 hierarchy.h/c
  └─ 修改 context_builder.c（选择性 L1/L3 加载）

Phase 5: 构建系统
  └─ 修改 CMakeLists.txt
```

---

## 十五、约束与限制

| 项目 | 限制 |
|------|------|
| Context buffer | 16KB |
| Agent stack | 24KB |
| 最大工具迭代 | 10 次/消息 |
| Session 历史 | 20 条消息 |
| SPIFFS 写入 | 有限次数（~10万次） |
| Skill index | ≤30 行，约 2KB |
| Auto skill 文件 | ≤100KB/个 |