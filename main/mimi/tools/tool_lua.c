/**
 * @file tool_lua.c
 * @brief Lua scripting tool - execute Lua scripts on ESP32
 *
 * This module provides tools for running Lua scripts:
 * - lua_eval: Execute a Lua code string directly
 * - lua_run: Execute a Lua script from FATFS file
 */

#include "tool_lua.h"
#include "mimi_config.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "lua_cjson.h"
#include "lua_socket_http.h"

static const char *TAG = "lua";

/* Output buffer size for Lua results */
#define LUA_OUTPUT_SIZE 4096

/**
 * @brief Capture print() output from Lua
 */
static int lua_print_wrapper(lua_State *L)
{
    int n = lua_gettop(L);
    char output[LUA_OUTPUT_SIZE] = {0};
    int offset = 0;

    for (int i = 1; i <= n; i++) {
        const char *s = lua_tostring(L, i);
        if (s) {
            if (offset > 0 && offset < (int)sizeof(output) - 1) {
                output[offset++] = '\t';
            }
            size_t len = strlen(s);
            if (offset + (int)len < (int)sizeof(output) - 1) {
                strcpy(output + offset, s);
                offset += len;
            }
        }
    }

    if (offset > 0 && offset < (int)sizeof(output) - 1) {
        output[offset++] = '\n';
    }

    output[offset] = '\0';
    ESP_LOGI(TAG, "Lua print: %s", output);

    return 0;
}

/**
 * @brief Create and configure a Lua state
 */
static lua_State *create_lua_state(void)
{
    lua_State *L = luaL_newstate();
    if (L == NULL) {
        ESP_LOGE(TAG, "Failed to create Lua state");
        return NULL;
    }

    luaL_openlibs(L);

    /* Register cjson module as global */
    luaL_requiref(L, "cjson", luaopen_cjson, 1);
    lua_pop(L, 1);

    /* Register socket.http module */
    /* First create socket table if it doesn't exist */
    lua_getglobal(L, "socket");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "socket");
    }
    /* Now load socket.http and set it in the socket table */
    luaL_requiref(L, "socket.http", luaopen_socket_http, 0);
    lua_setfield(L, -2, "http");
    lua_pop(L, 1);  /* pop socket table */

    /* Register print wrapper */
    lua_register(L, "print", lua_print_wrapper);

    /* Register global convenience functions */
    int err = luaL_dostring(L,
        "function http_get(url)\n"
        "    return socket.http.get(url)\n"
        "end\n"
        "function http_post(url, body, content_type)\n"
        "    return socket.http.post(url, body, content_type)\n"
        "end\n"
        "function json_decode(str)\n"
        "    return cjson.decode(str)\n"
        "end\n"
        "function json_encode(obj)\n"
        "    return cjson.encode(obj)\n"
        "end\n"
    );
    if (err != LUA_OK) {
        ESP_LOGE(TAG, "Failed to register global functions: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    /* Set package.path to include common locations */
    const char *path_setup = "package.path = package.path .. ';/fatfs/lua/?.lua;/fatfs/?.lua;./?.lua'";
    if (luaL_dostring(L, path_setup) != LUA_OK) {
        ESP_LOGW(TAG, "Failed to set package.path: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    return L;
}

/**
 * @brief Check if path is safe (starts with /fatfs/ and has no ..)
 */
static bool is_path_safe(const char *path)
{
    if (strncmp(path, MIMI_FATFS_BASE, strlen(MIMI_FATFS_BASE)) != 0) {
        return false;
    }
    if (strstr(path, "..") != NULL) {
        return false;
    }
    return true;
}

/**
 * @brief Serialize Lua stack values to JSON using cjson module
 *
 * Uses cjson.encode() to convert Lua values to JSON, then wraps
 * in {"result": [...]} format.
 */
static esp_err_t serialize_lua_results(lua_State *L, char *output, size_t output_size)
{
    int n = lua_gettop(L);
    if (n == 0) {
        snprintf(output, output_size, "{\"result\":null}");
        return ESP_OK;
    }

    /* Get cjson.encode function */
    lua_getglobal(L, "cjson");
    lua_getfield(L, -1, "encode");
    lua_remove(L, -2); /* remove cjson table */

    if (n == 1) {
        /* Single value: encode directly */
        lua_pushvalue(L, 1);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            snprintf(output, output_size, "{\"error\":\"cjson.encode failed\"}");
            lua_pop(L, 1);
            return ESP_ERR_INVALID_STATE;
        }
        const char *encoded = lua_tostring(L, -1);
        snprintf(output, output_size, "{\"result\":%s}", encoded ? encoded : "null");
        lua_pop(L, 1);
    } else {
        /* Multiple values: encode as array */
        lua_createtable(L, n, 0);
        for (int i = 1; i <= n; i++) {
            lua_pushvalue(L, i);
            lua_seti(L, -2, i);
        }
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            snprintf(output, output_size, "{\"error\":\"cjson.encode failed\"}");
            lua_pop(L, 1);
            return ESP_ERR_INVALID_STATE;
        }
        const char *encoded = lua_tostring(L, -1);
        snprintf(output, output_size, "{\"result\":%s}", encoded ? encoded : "[]");
        lua_pop(L, 1);
    }

    lua_pop(L, n);
    return ESP_OK;
}

/**
 * @brief Execute Lua code and capture output
 */
static esp_err_t execute_lua_code(lua_State *L, const char *code, char *output, size_t output_size)
{
    if (luaL_dostring(L, code) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        /* Use cJSON to properly escape the error string */
        cJSON *err_obj = cJSON_CreateString(err ? err : "unknown error");
        char *escaped = cJSON_PrintUnformatted(err_obj);
        cJSON_Delete(err_obj);
        snprintf(output, output_size, "{\"error\":%s}", escaped ? escaped : "\"unknown error\"");
        free(escaped);
        lua_pop(L, 1);
        return ESP_ERR_INVALID_STATE;
    }

    return serialize_lua_results(L, output, output_size);
}

/**
 * @brief Execute a Lua file and capture output
 */
static esp_err_t execute_lua_file(lua_State *L, const char *path, char *output, size_t output_size)
{
    if (luaL_dofile(L, path) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        cJSON *err_obj = cJSON_CreateString(err ? err : "unknown error");
        char *escaped = cJSON_PrintUnformatted(err_obj);
        cJSON_Delete(err_obj);
        snprintf(output, output_size, "{\"error\":%s}", escaped ? escaped : "\"unknown error\"");
        free(escaped);
        lua_pop(L, 1);
        return ESP_ERR_INVALID_STATE;
    }

    return serialize_lua_results(L, output, output_size);
}

esp_err_t tool_lua_eval_execute(const char *input_json, char *output, size_t output_size)
{
    if (!input_json || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "{\"error\":\"invalid JSON\"}");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *code_item = cJSON_GetObjectItem(root, "code");
    if (!code_item || !cJSON_IsString(code_item)) {
        snprintf(output, output_size, "{\"error\":\"missing 'code' field\"}");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const char *code = code_item->valuestring;
    ESP_LOGI(TAG, "Evaluating Lua code: %s", code);

    lua_State *L = create_lua_state();
    if (!L) {
        snprintf(output, output_size, "{\"error\":\"failed to create Lua state\"}");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = execute_lua_code(L, code, output, output_size);

    lua_close(L);
    cJSON_Delete(root);

    return ret;
}

esp_err_t tool_lua_run_execute(const char *input_json, char *output, size_t output_size)
{
    if (!input_json || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "{\"error\":\"invalid JSON\"}");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *path_item = cJSON_GetObjectItem(root, "path");
    if (!path_item || !cJSON_IsString(path_item)) {
        snprintf(output, output_size, "{\"error\":\"missing 'path' field\"}");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = path_item->valuestring;

    if (!is_path_safe(path)) {
        snprintf(output, output_size, "{\"error\":\"path must start with %s and not contain '..'\"}", MIMI_FATFS_BASE);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(output, output_size, "{\"error\":\"file not found: %s\"}", path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Running Lua script: %s", path);

    lua_State *L = create_lua_state();
    if (!L) {
        snprintf(output, output_size, "{\"error\":\"failed to create Lua state\"}");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = execute_lua_file(L, path, output, output_size);

    lua_close(L);
    cJSON_Delete(root);

    return ret;
}
