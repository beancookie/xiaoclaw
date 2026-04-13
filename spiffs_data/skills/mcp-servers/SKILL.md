---
name: mcp-servers
description: Connect to MCP servers and use remote tools
always: true
---

# MCP Servers

Connect to MCP (Model Context Protocol) servers to use remote tools.

## test_server

- host: 192.168.31.28
- port: 8080
- endpoint: mcp_server

## Available Tools

| Tool | Description |
|------|-------------|
| mcp_connect | Connect to an MCP server |
| mcp_disconnect | Disconnect from current server |

## How to Use

### Step 1: Connect to Server

```
Tool: mcp_connect
Input: {"server_name": "test_server"}
```

### Step 2: Discover Remote Tools

After connection, use `tools/call` to call `tools/list`:

```
Tool: mcp_server.tools_call
Input: {"name": "tools/list", "arguments": {}}
```

### Step 3: Call a Remote Tool

Use `tools/call` to call any discovered tool:

```
Tool: mcp_server.tools_call
Input: {"name": "self.echo", "arguments": {"message": "Hello"}}
```

### Step 4: Disconnect

```
Tool: mcp_disconnect
Input: {}
```

## Important

- Server must be running before connecting (192.168.31.28:8080)
- Use `mcp_disconnect` when done
- Tools are discovered dynamically via `tools/list`
