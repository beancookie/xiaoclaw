---
name: mcp-servers
description: Connect to MCP servers and use remote tools
always: false
---

# MCP Servers

Available MCP servers that mimi can connect to.

## test_server

- host: 192.168.31.27
- port: 8000
- endpoint: mcp

---

## How to Use

### Step 1: Connect to Server

```
Tool: mcp_connect
Input: {"server_name": "test_server"}
```

Connection flow (automatic):
1. Send `initialize` request
2. Send `tools/list` request to discover available tools
3. Register discovered tools with `{endpoint}.{tool_name}` prefix

On success, remote tools are automatically registered.

### Step 2: Call Remote Tools

Remote tools are available with prefix `{endpoint}.`:

| Remote Tool | Local Name | Parameters |
|-------------|------------|------------|
| get_device_status | mcp.get_device_status | none |
| set_led | mcp.set_led | color (string), brightness (0-100) |
| read_sensor | mcp.read_sensor | sensor_id (string), sensor_type (string) |
| control_motor | mcp.control_motor | speed (0-100), direction (string) |
| get_time | mcp.get_time | none |
| echo | mcp.echo | message (string) |

### Step 3: Disconnect

```
Tool: mcp_disconnect
Input: {}
```

### Example Calls

```
Tool: mcp_connect
Input: {"server_name": "test_server"}
```

```
Tool: mcp.echo
Input: {"message": "Hello"}
```

---

## Note

- Server must be running before connecting
- After connection, tools appear with `mcp.` prefix
- Use `mcp_disconnect` when done
