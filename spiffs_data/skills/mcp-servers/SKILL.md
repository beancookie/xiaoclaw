---
name: mcp-servers
description: Connect to MCP servers and use remote tools
always: true
---

# MCP Servers

This skill allows mimi to connect to MCP (Model Context Protocol) servers and use their tools. **When users ask about or request MCP connections, you MUST actively call the tools instead of just describing them.**

## Available Servers

### test_server

- host: 192.168.31.28
- port: 8080
- endpoint: mcp_server

---

## Agent Guidelines

### When to Use This Skill

- User asks "connect to MCP" or "连接 MCP"
- User asks "MCP server" or asks about remote tools
- User wants to use a tool that starts with `mcp_server.` (the endpoint name)
- User asks about connecting to external devices or services

### Important: ACT don't tell

**DO NOT** just tell the user "MCP connection is not implemented" or describe what would happen. **ACTUALLY CALL the tool.**

If user asks about MCP functionality, you should:
1. Call `mcp_connect` with `{"server_name": "test_server"}` to establish connection
2. Use the discovered tools (available with `{endpoint}.` prefix after connection, e.g., `mcp_server.echo`)
3. Report the actual result

---

## Tool Reference

### mcp_connect

Connect to an MCP server. Server must be defined in this skill file.

```
Tool: mcp_connect
Input: {"server_name": "test_server"}
```

### mcp_disconnect

Disconnect from current MCP server.

```
Tool: mcp_disconnect
Input: {}
```

---

## Usage Flow

### Step 1: Connect

```
Tool: mcp_connect
Input: {"server_name": "test_server"}
```

On success, remote tools are automatically discovered and registered with `{endpoint}.` prefix (e.g., `mcp_server.echo`).

### Step 2: Use Remote Tools

After connection, call tools with the `{endpoint}.` prefix based on the server's endpoint (e.g., `mcp_server.echo`). Tool schemas are discovered dynamically via `tools/list`.

### Step 3: Disconnect

```
Tool: mcp_disconnect
Input: {}
```

---

## Notes

- Server must be running before connecting
- After connection, tools appear with `{endpoint}.` prefix (e.g., `mcp_server.echo`)
- Use `mcp_disconnect` when done
- If connection fails, report the error message from the tool response
