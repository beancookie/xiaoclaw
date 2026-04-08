# Lua Scripts

Lua scripts are stored in `/spiffs/lua/` and can be executed using the `lua_run` tool.

## Available Scripts

| Script | Description |
|--------|-------------|
| hello.lua | Simple Hello World example |
| http_example.lua | HTTP GET/POST/PUT/DELETE examples |

## How to Execute

### Use `lua_run` Tool

```
Tool: lua_run
Input: {"path": "/spiffs/lua/hello.lua"}
```

### Available Lua Tools

When executing Lua scripts, the following functions are available:

| Function | Description | Parameters |
|----------|-------------|------------|
| print() | Print output to console | any values |
| http_get(url) | HTTP GET request | url (string) |
| http_post(url, body, content_type) | HTTP POST request | url, body, content_type |
| http_put(url, body, content_type) | HTTP PUT request | url, body, content_type |
| http_delete(url) | HTTP DELETE request | url |

All HTTP functions return: `(response_body, status_code)`

### Examples

```
Tool: lua_run
Input: {"path": "/spiffs/lua/hello.lua"}
```

```
Tool: lua_run
Input: {"path": "/spiffs/lua/http_example.lua"}
```

## lua_run vs lua_eval

- `lua_run`: Execute a script file from SPIFFS
  - Input: `{"path": "/spiffs/lua/script.lua"}`

- `lua_eval`: Execute Lua code string directly
  - Input: `{"code": "print('Hello')"}`

Use `lua_run` for stored scripts, use `lua_eval` for dynamic code.
