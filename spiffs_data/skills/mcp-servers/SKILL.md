---
name: mcp-servers
description: Connect to MCP servers and use remote tools
always: true
---
# MCP Servers

Connect to MCP (Model Context Protocol) servers to use remote tools. MCP allows you to access tools running on remote servers.

## Server Configuration

## test_server
- host: 192.168.31.28
- port: 8080
- endpoint: mcp_server

## Available MCP Tools

| Tool | When to Use |
|------|-------------|
| mcp_connect | When user asks to connect to an MCP server or use remote tools |
| mcp_disconnect | When user is done using remote tools or wants to switch servers |
| mcp_server.tools_list | After connecting, to discover what remote tools are available |
| mcp_server.tools_call | When you want to execute a specific remote tool |

## How MCP Works

1. **Connect first**: Use `mcp_connect` with `{"server_name": "test_server"}` to establish connection
2. **Discover tools**: After connecting, use `mcp_server.tools_list` to see available remote tools
3. **Call remote tools**: Use `mcp_server.tools_call` with `{"name": "tool_name", "arguments": {...}}` to execute
4. **Disconnect when done**: Use `mcp_disconnect` when finished

## Important

- Server must be running before connecting (192.168.31.28:8080)
- Always disconnect with `mcp_disconnect` when done
- Tools are discovered dynamically after connection via `mcp_server.tools_list`

## Note on Tool Calling

When you need to use an MCP tool, simply respond and the system will automatically invoke the tool through the proper mechanism. Describe what tool you want to use and why, and the tool will be executed automatically.
