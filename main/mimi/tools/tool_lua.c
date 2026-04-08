/**
 * @file tool_lua.c
 * @brief Lua scripting tool - execute Lua scripts on ESP32
 *
 * This module provides tools for running Lua scripts:
 * - lua_eval: Execute a Lua code string directly
 * - lua_run: Execute a Lua script from SPIFFS file
 */

#include "tool_lua.h"
#include "mimi_config.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"

static const char *TAG = "lua";

/* ── HTTP response buffer ─────────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *hb = (http_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        size_t needed = hb->len + evt->data_len;
        if (needed < hb->cap) {
            memcpy(hb->data + hb->len, evt->data, evt->data_len);
            hb->len += evt->data_len;
            hb->data[hb->len] = '\0';
        }
    }
    return ESP_OK;
}

#define HTTP_BUF_SIZE  (16 * 1024)

static int lua_http_request(lua_State *L, const char *method)
{
    const char *url = luaL_checkstring(L, 1);
    const char *body = (lua_gettop(L) >= 2) ? luaL_checkstring(L, 2) : NULL;
    const char *content_type = (lua_gettop(L) >= 3) ? luaL_optstring(L, 3, "text/plain") : NULL;

    http_buf_t hb = {0};
    hb.data = heap_caps_calloc(1, HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!hb.data) {
        return luaL_error(L, "out of memory");
    }
    hb.cap = HTTP_BUF_SIZE;

    esp_http_client_method_t client_method = HTTP_METHOD_GET;
    if (strcmp(method, "POST") == 0) client_method = HTTP_METHOD_POST;
    else if (strcmp(method, "PUT") == 0) client_method = HTTP_METHOD_PUT;
    else if (strcmp(method, "DELETE") == 0) client_method = HTTP_METHOD_DELETE;

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &hb,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(hb.data);
        return luaL_error(L, "failed to init HTTP client");
    }

    if (body) {
        esp_http_client_set_method(client, client_method);
        esp_http_client_set_header(client, "Content-Type", content_type);
        esp_http_client_set_post_field(client, body, strlen(body));
    } else if (strcmp(method, "DELETE") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_DELETE);
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        free(hb.data);
        return luaL_error(L, "HTTP request failed: %s", esp_err_to_name(err));
    }

    lua_pushstring(L, hb.data);
    lua_pushinteger(L, status);
    free(hb.data);
    return 2;
}

static int lua_http_get(lua_State *L)
{
    return lua_http_request(L, "GET");
}

static int lua_http_post(lua_State *L)
{
    return lua_http_request(L, "POST");
}

static int lua_http_put(lua_State *L)
{
    return lua_http_request(L, "PUT");
}

static int lua_http_delete(lua_State *L)
{
    return lua_http_request(L, "DELETE");
}

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

    /* Register print wrapper */
    lua_register(L, "print", lua_print_wrapper);

    /* Register HTTP wrappers */
    lua_register(L, "http_get", lua_http_get);
    lua_register(L, "http_post", lua_http_post);
    lua_register(L, "http_put", lua_http_put);
    lua_register(L, "http_delete", lua_http_delete);

    /* Set package.path to include common locations */
    const char *path_setup = "package.path = package.path .. ';/spiffs/lua/?.lua;/spiffs/?.lua;./?.lua'";
    if (luaL_dostring(L, path_setup) != LUA_OK) {
        ESP_LOGW(TAG, "Failed to set package.path: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    return L;
}

/**
 * @brief Execute Lua code and capture output
 */
static esp_err_t execute_lua_code(lua_State *L, const char *code, char *output, size_t output_size)
{
    if (luaL_dostring(L, code) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        snprintf(output, output_size, "{\"error\": \"%s\"}", err ? err : "unknown error");
        lua_pop(L, 1);
        return ESP_ERR_INVALID_STATE;
    }

    /* Check if there's a return value on the stack */
    int n = lua_gettop(L);
    if (n > 0) {
        /* Serialize return value(s) to output */
        char result[1024] = {0};
        int offset = 0;

        for (int i = 1; i <= n; i++) {
            int type = lua_type(L, i);
            const char *s = NULL;

            switch (type) {
                case LUA_TSTRING:
                    s = lua_tostring(L, i);
                    if (s) {
                        offset += snprintf(result + offset, sizeof(result) - offset,
                                         "%s%s", offset > 0 ? ", " : "", s);
                    }
                    break;
                case LUA_TNUMBER:
                    if (lua_isinteger(L, i)) {
                        offset += snprintf(result + offset, sizeof(result) - offset,
                                         "%s%d", offset > 0 ? ", " : "", (int)lua_tointeger(L, i));
                    } else {
                        offset += snprintf(result + offset, sizeof(result) - offset,
                                         "%s%.6g", offset > 0 ? ", " : "", lua_tonumber(L, i));
                    }
                    break;
                case LUA_TBOOLEAN:
                    offset += snprintf(result + offset, sizeof(result) - offset,
                                     "%s%s", offset > 0 ? ", " : "",
                                     lua_toboolean(L, i) ? "true" : "false");
                    break;
                case LUA_TNIL:
                    offset += snprintf(result + offset, sizeof(result) - offset,
                                     "%snil", offset > 0 ? ", " : "");
                    break;
                default:
                    offset += snprintf(result + offset, sizeof(result) - offset,
                                     "%s[%s]", offset > 0 ? ", " : "", lua_typename(L, type));
                    break;
            }
        }

        snprintf(output, output_size, "{\"result\": %s}", result);
        lua_pop(L, n);
    } else {
        snprintf(output, output_size, "{\"result\": null}");
    }

    return ESP_OK;
}

esp_err_t tool_lua_eval_execute(const char *input_json, char *output, size_t output_size)
{
    if (!input_json || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "{\"error\": \"invalid JSON\"}");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *code_item = cJSON_GetObjectItem(root, "code");
    if (!code_item || !cJSON_IsString(code_item)) {
        snprintf(output, output_size, "{\"error\": \"missing 'code' field\"}");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const char *code = code_item->valuestring;
    ESP_LOGI(TAG, "Evaluating Lua code: %s", code);

    lua_State *L = create_lua_state();
    if (!L) {
        snprintf(output, output_size, "{\"error\": \"failed to create Lua state\"}");
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
        snprintf(output, output_size, "{\"error\": \"invalid JSON\"}");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *path_item = cJSON_GetObjectItem(root, "path");
    if (!path_item || !cJSON_IsString(path_item)) {
        snprintf(output, output_size, "{\"error\": \"missing 'path' field\"}");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const char *path = path_item->valuestring;
    ESP_LOGI(TAG, "Running Lua script: %s", path);

    /* Validate path - must start with /spiffs/ */
    if (strncmp(path, MIMI_SPIFFS_BASE, strlen(MIMI_SPIFFS_BASE)) != 0) {
        snprintf(output, output_size, "{\"error\": \"path must start with %s\"}", MIMI_SPIFFS_BASE);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    /* Check if file exists */
    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(output, output_size, "{\"error\": \"file not found: %s\"}", path);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    lua_State *L = create_lua_state();
    if (!L) {
        snprintf(output, output_size, "{\"error\": \"failed to create Lua state\"}");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    if (luaL_dofile(L, path) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        snprintf(output, output_size, "{\"error\": \"%s\"}", err ? err : "unknown error");
        lua_pop(L, 1);
        lua_close(L);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }

    /* Script ran successfully, check for return values */
    int n = lua_gettop(L);
    if (n > 0) {
        char result[1024] = {0};
        int offset = 0;

        for (int i = 1; i <= n; i++) {
            int type = lua_type(L, i);
            const char *s = NULL;

            switch (type) {
                case LUA_TSTRING:
                    s = lua_tostring(L, i);
                    if (s) {
                        offset += snprintf(result + offset, sizeof(result) - offset,
                                         "%s\"%s\"", offset > 0 ? ", " : "", s);
                    }
                    break;
                case LUA_TNUMBER:
                    if (lua_isinteger(L, i)) {
                        offset += snprintf(result + offset, sizeof(result) - offset,
                                         "%s%d", offset > 0 ? ", " : "", (int)lua_tointeger(L, i));
                    } else {
                        offset += snprintf(result + offset, sizeof(result) - offset,
                                         "%s%.6g", offset > 0 ? ", " : "", lua_tonumber(L, i));
                    }
                    break;
                case LUA_TBOOLEAN:
                    offset += snprintf(result + offset, sizeof(result) - offset,
                                     "%s%s", offset > 0 ? ", " : "",
                                     lua_toboolean(L, i) ? "true" : "false");
                    break;
                case LUA_TNIL:
                    offset += snprintf(result + offset, sizeof(result) - offset,
                                     "%snil", offset > 0 ? ", " : "");
                    break;
                default:
                    offset += snprintf(result + offset, sizeof(result) - offset,
                                     "%s[%s]", offset > 0 ? ", " : "", lua_typename(L, type));
                    break;
            }
        }

        snprintf(output, output_size, "{\"result\": [%s]}", result);
        lua_pop(L, n);
    } else {
        snprintf(output, output_size, "{\"result\": null}");
    }

    lua_close(L);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Lua script executed successfully");
    return ESP_OK;
}
