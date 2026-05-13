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
    "## Global Functions\n\n" \
    "These functions are available directly, no require needed:\n\n" \
    "```lua\n" \
    "-- HTTP GET: returns response body, status code\n" \
    "local body, status = http_get(url)\n\n" \
    "-- HTTP POST: returns response body, status code\n" \
    "local body, status = http_post(url, body, content_type)\n\n" \
    "-- JSON decode: returns Lua table\n" \
    "local obj = json_decode(json_str)\n\n" \
    "-- JSON encode: returns JSON string\n" \
    "local json_str = json_encode(obj)\n" \
    "```\n"

#define DEFAULT_MCP_SERVERS_SKILL \
    "---\n" \
    "name: mcp-servers\n" \
    "description: Connect to MCP servers and use remote tools\n" \
    "always: true\n" \
    "---\n\n" \
    "# MCP Servers\n\n" \
    "## IMPORTANT: tenant_id Required\n\n" \
    "ALL remote MCP tools require `tenant_id` as a parameter. " \
    "You MUST include \"tenant_id\": " _MCP_XSTR(CONFIG_MIMI_MCP_REMOTE_TENANT_ID) " in every tool call's arguments. " \
    "This applies to both direct tool calls and mcp_server.tools_call.\n\n" \
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
    "3. Call: `mcp_server.tools_call` with `{\"name\": \"tool_name\", \"arguments\": {\"tenant_id\": " _MCP_XSTR(CONFIG_MIMI_MCP_REMOTE_TENANT_ID) ", ...}}`\n" \
    "4. Disconnect: `mcp_disconnect` when done\n"
