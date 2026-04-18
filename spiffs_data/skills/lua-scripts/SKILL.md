---
name: lua-scripts
description: Execute Lua scripts for custom logic and HTTP requests
always: false
---
# Lua Scripts

Execute Lua scripts stored on SPIFFS or run dynamic Lua code snippets.

## Available Scripts

| Script | Description |
|--------|-------------|
| hello.lua | Simple Hello World example |
| http_example.lua | HTTP GET/POST/PUT/DELETE examples |

## When to Use

Use Lua scripts when:
- User asks to run a stored script
- You need to perform HTTP requests beyond simple web search
- You need custom logic or calculations
- Testing quick Lua code snippets

## Available Lua Tools

| Tool | Purpose |
|------|---------|
| lua_run | Execute a stored Lua script from SPIFFS at `/spiffs/lua/<name>.lua` |
| lua_eval | Execute a Lua code string directly for quick snippets |

## Built-in Lua Functions

When running Lua code, these functions are available:

| Function | Description | Returns |
|----------|-------------|---------|
| print(...) | Output text | - |
| http_get(url) | HTTP GET request | (body, status_code) |
| http_post(url, body, content_type) | HTTP POST request | (body, status_code) |
| http_put(url, body, content_type) | HTTP PUT request | (body, status_code) |
| http_delete(url) | HTTP DELETE request | (body, status_code) |

## Note on Tool Calling

When you need to use lua_run or lua_eval, simply respond describing which tool you want to use and with what parameters. The system will automatically invoke the tool through the proper mechanism.
