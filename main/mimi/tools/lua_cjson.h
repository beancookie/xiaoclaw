/**
 * @file lua_cjson.h
 * @brief cjson Lua module header
 *
 * Provides JSON encode/decode functions for Lua scripts.
 * Wraps the cJSON C library.
 */

#pragma once

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/**
 * @brief cjson module entry point
 * @param L Lua state
 * @return Number of values returned on stack (1 - the module table)
 */
int luaopen_cjson(lua_State *L);
