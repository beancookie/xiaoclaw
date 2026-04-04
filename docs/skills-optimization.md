# Skills 架构优化计划

## 背景

参考 nanobot 项目的 skills 系统，优化 xiaoclaw 的 skills 架构。

## nanobot Skills 系统特点

1. **目录结构**: `skills/<name>/SKILL.md`
2. **YAML Frontmatter**: 包含 `name`, `description`, `metadata`
3. **"Always" Skills**: 标记为 `always=true` 的 skill 总是加载
4. **需求检查**: 检查 `requires.bins` (CLI工具) 和 `requires.env` (环境变量)
5. **结构化摘要**: XML 格式输出所有 skills

## nanobot Skill 文件格式

```yaml
---
name: cron
description: Schedule reminders and recurring tasks.
metadata: {"nanobot":{"emoji":"🕐","requires":{"bins":["curl"]}}}
---

# Cron

Use the `cron` tool to schedule...
```

## 当前 xiaoclaw 架构

- 扁平文件: `skills/xxx.md`
- 简单解析: 从 `# Title` 提取标题，从正文提取描述
- 无 metadata 支持

## 优化方案

### 1. 目录结构 + Frontmatter 解析

**文件**: `main/mimi/skills/skill_loader.c`

改动:
- 扫描 `skills/` 子目录，寻找 `SKILL.md` 文件
- 解析 YAML frontmatter (简化版)
- 提取 `name`, `description`, `always`, `requires`

```c
typedef struct {
    char name[32];
    char description[128];
    bool always;           // 是否总是加载
    bool has_requirements; // 是否有依赖
    char path[256];       // 完整文件路径
} skill_info_t;
```

### 2. "Always" Skills 支持

添加 `skill_loader_get_always_skills()` 函数:
- 返回标记为 `always=true` 的 skill 列表
- 这些 skill 的内容会始终注入 system prompt

### 3. 需求检查（简化）

添加 `skill_loader_check_requirements()`:
- 检查 skill 声明的 `requires.bins` 是否存在
- ESP32 上主要检查 env vars (API keys 等)

### 4. 结构化摘要格式

输出改进为 XML 格式:

```
<skills>
  <skill available="true">
    <name>weather</name>
    <description>Get current weather</description>
    <location>/spiffs/skills/weather/SKILL.md</location>
  </skill>
</skills>
```

## 需要修改的文件

| 文件 | 改动 |
|------|------|
| `main/mimi/skills/skill_loader.h` | 添加新 API 声明 |
| `main/mimi/skills/skill_loader.c` | 目录扫描 + frontmatter 解析 + always skills |

## 新增 API

```c
/**
 * 列出所有可用 skills
 * @param skills 输出数组 (调用者分配)
 * @param max    最大数量
 * @return 实际数量
 */
int skill_loader_list(skill_info_t *skills, int max);

/**
 * 加载指定 skill 内容
 * @param name skill 名称
 * @param buf  输出 buffer
 * @param size buffer 大小
 * @return ESP_OK on success
 */
esp_err_t skill_loader_load(const char *name, char *buf, size_t size);

/**
 * 获取 "always" skills 的内容 (用 --- 分隔)
 */
size_t skill_loader_get_always_content(char *buf, size_t size);

/**
 * 检查 skill 需求是否满足
 */
bool skill_loader_check_skill(const char *name);
```

## 实施步骤

1. 定义 `skill_info_t` 结构
2. 实现 `skill_loader_list()` - 扫描目录，解析 frontmatter
3. 实现 `skill_loader_load()` - 加载单个 skill
4. 实现 `skill_loader_get_always_content()` - 获取 always skills
5. 更新 `context_builder.c` - 使用新的 skills API
6. 更新 skills 目录结构 (需要用户配合)

## 验证

- `idf.py build` 成功
- 列出 skills 时正确显示 metadata
- "always" skills 被正确注入 system prompt
