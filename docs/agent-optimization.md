# Agent 优化计划

## 背景

当前 xiaoclaw 的 agent 实现（C/ESP32）在 `main/mimi/agent/agent_loop.c` 中是单文件解决方案，混合了：prompt 构建、session 管理、ReAct 循环和工具执行。

参考 [nanobot](https://github.com/anobot/nanobot) 项目的架构，采用清晰的分层设计。

## 参考架构对比

| 组件 | nanobot (Python) | xiaoclaw (当前) |
|------|------------------|-----------------|
| 执行引擎 | `AgentRunner` | 内嵌在 `agent_loop.c` |
| 上下文构建 | `ContextBuilder` | `context_builder.c` (硬编码) |
| 内存 | `MemoryStore` + `Consolidator` | `memory_store.c` (简单) |
| 工具注册 | `ToolRegistry` | `tool_registry.c` |
| 历史记录 | `history.jsonl` + cursor | `session.c` (内存JSON) |
| Hook系统 | `AgentHook` | 无 |
| 检查点 | 支持（崩溃恢复） | 无 |
| 并发工具 | 支持 | 无（串行） |

## 优化方案

### 1. Agent Runner 重构

**文件:** `main/mimi/agent/agent_loop.c` → 拆分为 `runner.c` + `loop.c`

创建可复用的 `AgentRunner`：
- 接收 `AgentRunSpec`（model、tools、max_iterations、hooks 等）
- 返回 `AgentRunResult`（final_content、messages、tools_used、usage、stop_reason）
- 支持上下文窗口裁剪（接近 token 限制时修剪历史）
- 支持检查点回调用于恢复

```c
esp_err_t agent_runner_run(AgentRunSpec *spec, AgentRunResult *result);
```

### 2. Context Builder 增强

**文件:** `main/mimi/agent/context_builder.c`

重构为模块化设计：
- `build_system_prompt()` 与消息构建分离
- 添加运行时上下文注入（时间、channel、chat_id），使用标签标记
- 加载 bootstrap 文件（SOUL.md、USER.md），带清晰的分节头
- 分别构建 skills 摘要
- 支持 "always-on" skills 和 "available" skills

### 3. Tool Registry 改进

**文件:** `main/mimi/tools/tool_registry.c`, `main/mimi/tools/*.c`

- 为每个工具添加 `concurrency_safe` 标志
- 通过 `xTaskCreatePinnedToCore` 支持安全工具的并发执行
- 添加 `prepare_call` 钩子用于执行前验证

### 4. 内存系统增强

**文件:** `main/mimi/memory/memory_store.c`, `main/mimi/memory/session_mgr.c`

- 添加每个 session 的 token 预算跟踪
- 添加 `Consolidator` 风格的历史压缩（未来：LLM 摘要；立即：简单截断）
- 添加检查点保存/恢复用于崩溃恢复
- 使用 JSONL 格式和 cursor 实现追加历史

### 5. Hook 系统（简化版）

**文件:** `main/mimi/agent/hook.h`

实现最小化钩子系统：

```c
typedef struct {
    void (*before_iteration)(int iteration);
    void (*after_iteration)(int iteration, const char *final_content);
    void (*on_tool_result)(const char *tool_name, const char *result);
} AgentHooks;
```

### 6. 检查点系统

**文件:** `main/mimi/agent/checkpoint.c`

保存中间状态到 session：
- 每次工具执行后，保存 assistant message + pending tool calls
- 崩溃恢复时，从检查点恢复并继续
- 防止掉电丢失工作

## 需要修改的文件

| 文件 | 改动 |
|------|------|
| `main/mimi/agent/agent_loop.c` | 重构：提取 runner 逻辑，简化 loop |
| `main/mimi/agent/context_builder.c` | 增强：模块化 prompt 构建 |
| `main/mimi/agent/hook.h` | 新增：hook 系统接口 |
| `main/mimi/agent/runner.h` | 新增：runner 接口 |
| `main/mimi/agent/runner.c` | 新增：核心执行引擎 |
| `main/mimi/agent/checkpoint.h` | 新增：检查点接口 |
| `main/mimi/agent/checkpoint.c` | 新增：检查点实现 |
| `main/mimi/memory/session_mgr.c` | 新增：token 预算跟踪，检查点保存/加载 |
| `main/mimi/tools/tool_registry.c` | 新增：`concurrency_safe` 标志，`prepare_call` 钩子 |

## 实施顺序

### Phase 1: Runner 模块
- 创建 `runner.h`、`runner.c`
- 从 `agent_loop.c` 提取 ReAct 循环逻辑
- 添加上下文窗口裁剪

### Phase 2: Context Builder 增强
- 模块化系统 prompt 各部分
- 添加运行时上下文标签

### Phase 3: Tool Registry 改进
- 添加 concurrency 标志
- 添加工具准备钩子

### Phase 4: Hook 系统 + 检查点
- 基础 hooks 用于日志
- Session 检查点保存/恢复

### Phase 5: 简化 agent_loop.c
- 使用 runner 替代内联循环
- 清理消息处理

## 验证

- 构建：`idf.py build` 成功
- 烧录到 ESP32-S3 运行
- 测试工具功能（web_search、文件操作、gpio）
- 测试多轮对话保留上下文
- 验证长时间运行无内存泄漏
