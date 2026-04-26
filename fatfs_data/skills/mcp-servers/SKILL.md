---
name: mcp-servers
description: Connect to MCP servers and use remote tools
always: true
---

# MCP Servers

Connect to MCP (Model Context Protocol) servers to use remote tools. MCP allows you to access tools running on remote servers.

## Server Configuration

Server addresses can be configured via:
1. **Kconfig** (recommended): Set `CONFIG_MIMI_MCP_REMOTE_HOST`, `CONFIG_MIMI_MCP_REMOTE_PORT`, etc.
2. **SKILL.md** (legacy): Define servers in this file for backward compatibility

## Available MCP Tools

| Tool                  | When to Use                                                     |
| --------------------- | --------------------------------------------------------------- |
| mcp_connect           | When user asks to connect to an MCP server or use remote tools  |
| mcp_disconnect        | When user is done using remote tools or wants to switch servers |
| mcp_server.tools_list | After connecting, to discover what remote tools are available   |
| mcp_server.tools_call | When you want to execute a specific remote tool                 |

## How MCP Works

1. **Connect first**: Use `mcp_connect` with `{"server_name": "default"}` to establish connection (uses Kconfig server address)
2. **Discover tools**: After connecting, use `mcp_server.tools_list` to see available remote tools
3. **Call remote tools**: Use `mcp_server.tools_call` with `{"name": "tool_name", "arguments": {...}}` to execute
4. **Disconnect when done**: Use `mcp_disconnect` when finished

## Important

- Server must be running before connecting (address configured in Kconfig)
- Always disconnect with `mcp_disconnect` when done
- Tools are discovered dynamically after connection via `mcp_server.tools_list`

## Note on Tool Calling

When you need to use an MCP tool, simply respond and the system will automatically invoke the tool through the proper mechanism. Describe what tool you want to use and why, and the tool will be executed automatically.

## Response Format

MCP 工具返回结果的 `content` 数组中，每个元素包含：
- `type`: 内容类型（如 "text"）
- `text`: 实际数据，可能是嵌套的 JSON 字符串

**解析方法**：如果 `text` 是 JSON 字符串，需要先解析外层 JSON，然后从 `content[0].text` 中提取实际结果数据。

**示例**：
```
Response: {"content":[{"type":"text","text":"{...}"}]}
```
提取方式：`content[0].text` 内的 JSON 即为实际返回数据。

常见响应结构：
- `success`: boolean，是否成功
- `total`: int，总数
- `groups`: array，统计分组
