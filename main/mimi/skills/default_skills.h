#pragma once

/* Default SKILL.md content for built-in skills */

/* Stringification helpers for embedding Kconfig values */
#define _MCP_STR(x) #x
#define _MCP_XSTR(x) _MCP_STR(x)

#define DEFAULT_LUA_SCRIPTS_SKILL \
    "---\n" \
    "name: lua-scripts\n" \
    "description: Execute Lua scripts for custom logic and HTTP requests\n" \
    "always: false\n" \
    "---\n" \
    "# Lua Scripts\n\n" \
    "Execute Lua scripts stored on FATFS or run dynamic Lua code snippets.\n\n" \
    "## Available Tools\n\n" \
    "| Tool | Purpose |\n" \
    "|------|----------|\n" \
    "| lua_run | Execute a stored Lua script from FATFS |\n" \
    "| lua_eval | Execute a Lua code string directly |\n\n" \
    "## Built-in Modules\n\n" \
    "### socket.http\n\n" \
    "HTTP client module.\n\n" \
    "```lua\n" \
    "local http = require(\"socket.http\")\n\n" \
    "-- GET: returns response body, status code\n" \
    "local body, status = http.get(url)\n\n" \
    "-- POST: returns response body, status code\n" \
    "local body, status = http.post(url, payload, content_type)\n\n" \
    "-- General request (GET/POST/PUT/DELETE)\n" \
    "local body, status = http.request(url)\n" \
    "local body, status = http.request{url=url, method=\"POST\", body=data}\n" \
    "```\n\n" \
    "### cjson\n\n" \
    "JSON encode/decode.\n\n" \
    "```lua\n" \
    "local cjson = require(\"cjson\")\n" \
    "local json_str = cjson.encode({key = \"value\"})\n" \
    "local obj = cjson.decode(json_str)\n" \
    "print(obj.key)\n" \
    "```\n"

#define DEFAULT_MCP_SERVERS_SKILL \
    "---\n" \
    "name: mcp-servers\n" \
    "description: Connect to MCP servers and use remote tools\n" \
    "always: true\n" \
    "---\n\n" \
    "# MCP Servers\n\n" \
    "Connect to MCP servers to use remote tools.\n\n" \
    "## Available Tools\n\n" \
    "| Tool | When to Use |\n" \
    "|------|------------|\n" \
    "| mcp_connect | Connect to an MCP server |\n" \
    "| mcp_disconnect | Disconnect from server |\n" \
    "| mcp_server.tools_list | Discover available remote tools |\n" \
    "| mcp_server.tools_call | Execute a remote tool |\n\n" \
    "## How to Use\n\n" \
    "1. Connect: `mcp_connect` with `{\"server_name\": \"default\"}`\n" \
    "2. Discover: `mcp_server.tools_list`\n" \
    "3. Call: `mcp_server.tools_call` with `{\"name\": \"tool_name\", \"arguments\": {...}}`\n" \
    "4. Disconnect: `mcp_disconnect` when done\n\n" \
    "## Important\n\n" \
    "When calling any remote tool via mcp_server.tools_call, " \
    "always include tenant_id=" _MCP_XSTR(CONFIG_MIMI_MCP_REMOTE_TENANT_ID) " in the arguments.\n"
