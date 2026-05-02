/**
 * @file lua_cjson.c
 * @brief cjson Lua module implementation
 *
 * Provides JSON encode/decode functions wrapping cJSON C library.
 */

#include "lua_cjson.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief Recursively convert Lua value to cJSON
 */
static cJSON *lua_to_cjson(lua_State *L, int idx)
{
    int type = lua_type(L, idx);

    switch (type) {
        case LUA_TTABLE: {
            /* Check if array or object */
            lua_len(L, idx);
            int len = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);

            int is_array = 1;
            if (len == 0) {
                /* Check if has any numeric keys or non-sequential keys */
                lua_pushnil(L);
                while (lua_next(L, idx) != 0) {
                    if (lua_type(L, -2) != LUA_TNUMBER) {
                        is_array = 0;
                    } else {
                        /* Check if key is sequential number */
                        lua_Number n = lua_tonumber(L, -2);
                        if (n < 1 || n != (int)n) {
                            is_array = 0;
                        }
                    }
                    lua_pop(L, 1);
                    if (!is_array) break;
                }
            }

            cJSON *json;
            if (is_array && len > 0) {
                json = cJSON_CreateArray();
                for (int i = 1; i <= len; i++) {
                    lua_geti(L, idx, i);
                    cJSON *item = lua_to_cjson(L, -1);
                    lua_pop(L, 1);
                    if (item) {
                        cJSON_AddItemToArray(json, item);
                    } else {
                        cJSON_Delete(json);
                        return NULL;
                    }
                }
            } else {
                json = cJSON_CreateObject();
                lua_pushnil(L);
                while (lua_next(L, idx) != 0) {
                    /* Key must be string or number */
                    const char *key = NULL;
                    char key_buf[32];

                    if (lua_type(L, -2) == LUA_TSTRING) {
                        key = lua_tostring(L, -2);
                    } else if (lua_type(L, -2) == LUA_TNUMBER) {
                        snprintf(key_buf, sizeof(key_buf), "%d", (int)lua_tointeger(L, -2));
                        key = key_buf;
                    } else {
                        /* Skip invalid key type */
                        lua_pop(L, 1);
                        continue;
                    }

                    cJSON *value = lua_to_cjson(L, -1);
                    if (value) {
                        cJSON_AddItemToObject(json, key, value);
                    }
                    lua_pop(L, 1);
                }
            }
            return json;
        }

        case LUA_TSTRING:
            return cJSON_CreateString(lua_tostring(L, idx));

        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) {
                return cJSON_CreateNumber((double)lua_tointeger(L, idx));
            } else {
                return cJSON_CreateNumber(lua_tonumber(L, idx));
            }

        case LUA_TBOOLEAN:
            return cJSON_CreateBool(lua_toboolean(L, idx));

        case LUA_TNIL:
            return cJSON_CreateNull();

        default:
            return NULL;
    }
}

/**
 * @brief Recursively convert cJSON to Lua value
 */
static int cjson_to_lua(lua_State *L, cJSON *item)
{
    if (!item) {
        lua_pushnil(L);
        return 1;
    }

    if (cJSON_IsObject(item)) {
        lua_newtable(L);
        cJSON *child = item->child;
        while (child) {
            lua_pushstring(L, child->string);
            cjson_to_lua(L, child);
            lua_settable(L, -3);
            child = child->next;
        }
    } else if (cJSON_IsArray(item)) {
        int len = cJSON_GetArraySize(item);
        lua_createtable(L, len, 0);
        int i = 1;
        cJSON *child = item->child;
        while (child) {
            cjson_to_lua(L, child);
            lua_seti(L, -2, i);
            i++;
            child = child->next;
        }
    } else if (cJSON_IsString(item)) {
        lua_pushstring(L, item->valuestring);
    } else if (cJSON_IsNumber(item)) {
        /* Check if it's an integer by seeing if valuedouble equals valueint */
        if (item->valuedouble == item->valueint && (int)item->valueint == item->valuedouble) {
            lua_pushinteger(L, item->valueint);
        } else {
            lua_pushnumber(L, item->valuedouble);
        }
    } else if (cJSON_IsTrue(item)) {
        lua_pushboolean(L, 1);
    } else if (cJSON_IsFalse(item)) {
        lua_pushboolean(L, 0);
    } else if (cJSON_IsNull(item)) {
        lua_pushnil(L);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

/**
 * @brief Encode Lua value to JSON string
 * cjson.encode(value) -> json_string
 */
static int lua_cjson_encode(lua_State *L)
{
    cJSON *json = lua_to_cjson(L, 1);
    if (!json) {
        return luaL_error(L, "failed to encode value to JSON");
    }

    char *str = cJSON_Print(json);
    cJSON_Delete(json);

    if (!str) {
        return luaL_error(L, "failed to print JSON");
    }

    lua_pushstring(L, str);
    free(str);
    return 1;
}

/**
 * @brief Decode JSON string to Lua value
 * cjson.decode(json_string) -> value
 */
static int lua_cjson_decode(lua_State *L)
{
    const char *str = luaL_checkstring(L, 1);

    cJSON *json = cJSON_Parse(str);
    if (!json) {
        return luaL_error(L, "JSON parse error");
    }

    int ret = cjson_to_lua(L, json);
    cJSON_Delete(json);
    return ret;
}

/**
 * @brief cjson module entry point
 */
int luaopen_cjson(lua_State *L)
{
    luaL_Reg funcs[] = {
        {"encode", lua_cjson_encode},
        {"decode", lua_cjson_decode},
        {NULL, NULL}
    };

    luaL_newlib(L, funcs);
    return 1;
}
