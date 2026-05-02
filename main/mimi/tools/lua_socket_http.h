/**
 * @file lua_socket_http.h
 * @brief socket.http Lua module header
 *
 * Provides LuaSocket-compatible HTTP client API.
 * Wraps the existing lua_http_request function.
 */

#pragma once

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/**
 * @brief socket module entry point
 * @param L Lua state
 * @return Number of values returned on stack (1 - the socket table with http subtable)
 */
int luaopen_socket(lua_State *L);
