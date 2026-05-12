/**
 * @file lua_socket_http.h
 * @brief socket.http Lua module header
 *
 * Provides LuaSocket-compatible HTTP client API.
 */

#pragma once

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/**
 * @brief socket.http module entry point
 * @param L Lua state
 * @return 1 - the http table with get, post, request, etc.
 */
int luaopen_socket_http(lua_State *L);
