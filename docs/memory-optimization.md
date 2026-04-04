# Memory 系统优化计划

## 完成状态

| Phase | 状态 | 说明 |
|-------|------|------|
| Phase 1: Session Manager 增强 | ✅ 完成 | metadata、cursor、history alignment |
| Phase 2: Consolidator | ✅ 完成 | 简化版压缩（不需 LLM）|
| Phase 3: Cursor 机制 | ✅ 完成 | session_read_after_cursor |
| Phase 4: 内存优化 | 🔄 进行中 | Session 缓存 |
| Phase 5: Checkpoint 集成 | ⏳ 待定 | 与 checkpoint.c 集成 |

---

## 背景

参考 nanobot 项目的 memory 系统，优化 xiaoclaw 的 memory 和 session 管理。

## nanobot Memory 系统特点

### 1. MemoryStore
- **文件结构**: `memory/MEMORY.md`, `history.jsonl`, `.cursor`
- **append-only**: 使用 JSONL 格式追加历史
- **cursor 机制**: 跟踪已处理的历史位置
- **compact**: 超过 `max_history_entries` 时压缩历史

### 2. SessionManager
- **Session 结构**: key (channel:chat_id), messages, metadata, last_consolidated
- **JSONL 持久化**: 每个 session 存储为独立 JSONL 文件
- **内存缓存**: `_cache` 字典缓存活跃 session
- **history alignment**: 只返回未 consolidate 的消息

### 3. Consolidator
- **token budget 触发**: 上下文快满时自动压缩
- **archive 流程**: LLM 总结旧消息 → 追加到 history.jsonl
- **5轮循环**: 每轮压缩到预算一半
- **fallback**: LLM 失败时 raw dump 到历史

### 4. Dream (深度记忆)
- **cron 触发**: 定期分析历史，编辑 MEMORY.md/SOUL.md/USER.md
- **两阶段**: 分析(LLM) → 编辑(AgentRunner)
- **原子事实**: 输出 `[FILE] 事实` 或 `[SKIP]`
- **batch 处理**: 每次最多 20 条

### 5. Runtime Checkpoint
- **session.metadata**: 存储 `runtime_checkpoint`
- **恢复机制**: 中断后恢复未完成的 tool 调用
- **pending tools**: 标记为 `"Error: Task interrupted"`

## 当前 xiaoclaw 架构

### memory_store.c
- 简单文件读写: MEMORY.md, daily/*.md
- 无 cursor 机制
- 无压缩/总结

### session_mgr.c
- JSONL 格式追加
- 环形缓冲区读取 (最多 MIMI_SESSION_MAX_MSGS)
- 无 metadata 支持
- 无 consolidation

## 优化方案

### Phase 1: 增强 Session Manager

**文件**: `main/mimi/memory/session_mgr.c`

1. **添加 session metadata**
```c
typedef struct {
    char key[128];                    // channel:chat_id
    int last_consolidated;            // 已总结的消息数
    int cursor;                      // 历史游标
    time_t updated_at;
} session_metadata_t;
```

2. **添加 session 加载/保存**
```c
esp_err_t session_load(const char *chat_id, session_t *session);
esp_err_t session_save(session_t *session);
```

3. **添加 history alignment**
```c
esp_err_t session_get_unconsolidated(const char *chat_id, char *buf, size_t size);
```

### Phase 2: 添加 Consolidator (简化版)

**文件**: `main/mimi/memory/consolidator.c`

由于 ESP32 资源限制，实现简化版：
- 简单 token 计数 (字符数 / 4)
- 固定阈值触发压缩
- 直接截断旧消息，不做 LLM 总结
- 将压缩后的消息追加到 archive 文件

```c
typedef struct {
    int max_history;        // 最大历史条数
    int archive_threshold;   // 压缩阈值
} consolidator_config_t;

esp_err_t consolidator_init(consolidator_config_t *config);
esp_err_t consolidator_maybe_consolidate(session_t *session);
```

### Phase 3: 添加 Cursor 机制

**文件**: `main/mimi/memory/session_mgr.c`

```c
// 读取未处理的历史 (cursor 之后)
esp_err_t session_read_unprocessed(const char *chat_id, int cursor,
                                   char *buf, size_t size, int *next_cursor);

// 推进 cursor
esp_err_t session_advance_cursor(const char *chat_id, int new_cursor);
```

### Phase 4: 内存优化

1. **合并 memory_store + session_mgr**
   - 减少重复代码
   - 统一文件操作

2. **添加 session 缓存限制**
   - 限制最大缓存 session 数
   - LRU 淘汰

### Phase 5: Checkpoint 集成

已有 `checkpoint.c`，现在与 session 集成：
```c
esp_err_t session_save_checkpoint(const char *chat_id, checkpoint_phase_t phase,
                                  int iteration, cJSON *checkpoint);
esp_err_t session_load_checkpoint(const char *chat_id, ...);
```

## 需要修改的文件

| 文件 | 改动 |
|------|------|
| `main/mimi/memory/session_mgr.c` | 增强 session 管理，添加 cursor/metadata |
| `main/mimi/memory/session_mgr.h` | 新增 API 声明 |
| `main/mimi/memory/consolidator.c` | 新增：简化压缩器 |
| `main/mimi/memory/consolidator.h` | 新增：压缩器接口 |
| `main/mimi/CMakeLists.txt` | 添加新文件 |

## 新增 API

```c
/* Session metadata */
esp_err_t session_get_metadata(const char *chat_id, session_metadata_t *meta);
esp_err_t session_save_metadata(const char *chat_id, session_metadata_t *meta);

/* History alignment */
esp_err_t session_get_unconsolidated(const char *chat_id, char *buf, size_t size, int *remaining);
esp_err_t session_mark_consolidated(const char *chat_id, int count);

/* Cursor */
esp_err_t session_read_after_cursor(const char *chat_id, int cursor,
                                   char *buf, size_t size, int *next_cursor);
esp_err_t session_advance_cursor(const char *chat_id, int new_cursor);

/* Consolidator */
esp_err_t consolidator_init(consolidator_config_t *config);
esp_err_t consolidator_check_and_run(const char *chat_id);
```

## 实施步骤

1. **Phase 1a**: 定义 session_metadata_t，添加 session_load/save
2. **Phase 1b**: 实现 session_get_unconsolidated
3. **Phase 2**: 创建 consolidator.c (简化版)
4. **Phase 3**: 实现 cursor 读写
5. **Phase 4**: 内存缓存优化
6. **Phase 5**: 与 checkpoint 集成

## 验证

- `idf.py build` 成功
- 多轮对话后历史正确累积
- Consolidation 触发时正确压缩历史
- Checkpoint 恢复后 session 状态正确

## 限制说明

由于 ESP32 资源限制，以下 nanobot 功能**不实现**：
- LLM 驱动的总结 (需要额外 API 调用)
- Git 版本控制 (太复杂)
- Dream 深度记忆 (需要 cron + agent 协同)
- 复杂的 token 预算计算

这些功能可以在更强大的平台上实现。
