# Skills 参考指南

本文档介绍如何编写 Skill 文件（SKILL.md），包括格式规范、字段说明、推荐结构及最佳实践。

## 完整格式规范

```yaml
---
name: <skill-name>
description: <brief description of what the skill does>
always: <true|false>
---

# Skill Title

Brief introduction paragraph explaining what this skill does.

## When to Use

Describe conditions that trigger this skill or when to recommend it.

## Available Tools (optional)

List any tools specific to this skill.

| Tool | Purpose |
|------|---------|

## How to Use

Step-by-step instructions for using the skill.

## Examples (optional)

### Example 1

### Example 2
```

---

## Frontmatter 字段详解

### name

| 属性 | 值 |
|------|---|
| 类型 | string |
| 必填 | 是 |
| 格式 | 小写字母、数字、连字符（`a-z0-9-`） |
| 限制 | 最大 31 字符，需与目录名一致 |

**示例**: `name: lua-scripts`, `name: mcp-servers`, `name: my-custom-skill`

### description

| 属性 | 值 |
|------|---|
| 类型 | string |
| 必填 | 是 |
| 格式 | 一句话简短描述 |
| 限制 | 最大 127 字符 |

描述出现在 skills 摘要 XML 中，应简洁明了。

**好示例**: `Execute Lua scripts for custom logic and HTTP requests`
**坏示例**: `This skill allows you to execute Lua scripts that are stored on SPIFFS or run dynamic Lua code snippets for performing custom logic and making HTTP requests`

### always

| 属性 | 值 |
|------|---|
| 类型 | boolean |
| 必填 | 否 |
| 默认值 | `false` |
| 说明 | `true` 时 skill 内容始终注入 system prompt |

NOTICE: 仅对需要频繁使用的 skills 设置 `always: true`。过多 always skills 会增加 system prompt 长度，影响性能。

---

## 推荐内容结构

### 1. When to Use

描述触发该 skill 的条件或场景。帮助 Agent 判断何时应使用该 skill。

```markdown
## When to Use

Use this skill when:
- User asks to create a new skill or teach the bot something new
- User wants to add a custom capability or workflow
- User asks to save instructions for later use
```

### 2. Available Tools

列出该 skill 相关的工具（如有）。

```markdown
## Available Tools

| Tool | When to Use |
|------|-------------|
| lua_run | Execute a stored Lua script from SPIFFS |
| lua_eval | Execute a Lua code string directly |
```

### 3. How to Use

分步骤说明如何使用该 skill。

```markdown
## How to Use

1. Step one
2. Step two
3. Step three
```

### 4. Examples（可选）

提供具体使用示例。

```markdown
## Examples

### Example 1

{
  "script_name": "hello"
}

### Example 2

{
  "code": "print('Hello, World!')"
}
```

---

## 编写最佳实践

### Do's

- **简洁描述**: 描述应简洁，控制在 10-15 词
- **清晰触发条件**: When to Use 部分应列出具体场景
- **步骤化说明**: How to Use 使用编号列表
- **注明工具限制**: 如有依赖，注明前提条件

### Don'ts

- **避免冗长**: 不要复制太多文档内容
- **不要过度设计**: 不要添加过多层级标题
- **不要省略 frontmatter**: 即使觉得简单也要保留

---

## 故障排除

### Skill 未出现在列表中

1. 检查文件路径是否为 `/spiffs/skills/<name>/SKILL.md`
2. 检查 frontmatter 是否使用 `---` 包裹
3. 检查 `name` 字段是否与目录名一致
4. 重启设备使 skill_loader 重新扫描

### Skill 内容未加载

1. 确认 skill 不是 `always: true`（仅在 system prompt 中注入 always skills）
2. 使用 `read_file` 工具按需加载 skill 内容
3. 检查文件内容是否正确解析

### frontmatter 解析失败

确保 frontmatter 格式正确：
- 使用 `---` 作为开始和结束标记
- 字段名和值之间使用冒号和空格 (`key: value`)
- 布尔值使用小写 (`true`/`false`)

---

## 完整示例

```yaml
---
name: weather
description: Get current weather information for any location
always: false
---

# Weather Skill

Get current weather information, forecasts, and weather-related alerts.

## When to Use

Use this skill when:
- User asks about current weather
- User wants a weather forecast
- User asks for weather in a specific city or location
- User needs clothing or activity recommendations based on weather

## Available Tools

| Tool | Purpose |
|------|---------|
| weather_get | Get current weather for a location |
| weather_forecast | Get weather forecast for upcoming days |

## How to Use

1. Extract location from user query
2. Call weather_get with the location
3. Interpret results and provide friendly summary
4. Offer additional info (forecast, recommendations) if relevant

## Note on Tool Calling

When you need to use weather tools, simply respond describing what tool you want to use and with what parameters. The system will automatically invoke the tool through the proper mechanism.
```

---

## 相关文档

- [Skills 系统架构](./skills-architecture.md) - 系统设计说明
- [内置 Skills](./skills-builtin.md) - 内置 Skills 详细说明
