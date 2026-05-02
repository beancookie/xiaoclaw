/**
 * @file lua_socket_http.c
 * @brief socket.http Lua module implementation
 *
 * Provides LuaSocket-compatible HTTP client API.
 * Uses ESP-IDF's esp_http_client for HTTP requests.
 */

#include "lua_socket_http.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"

#define HTTP_BUF_SIZE  (16 * 1024)

/* ── HTTP response buffer ─────────────────────────────────────── */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    bool truncated;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *hb = (http_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        size_t needed = hb->len + evt->data_len;
        if (needed >= hb->cap) {
            hb->truncated = true;
            size_t copy_len = hb->cap - hb->len - 1;
            if (copy_len > 0) {
                memcpy(hb->data + hb->len, evt->data, copy_len);
                hb->len += copy_len;
                hb->data[hb->len] = '\0';
            }
        } else {
            memcpy(hb->data + hb->len, evt->data, evt->data_len);
            hb->len += evt->data_len;
            hb->data[hb->len] = '\0';
        }
    }
    return ESP_OK;
}

static int http_request(lua_State *L, const char *method, const char *url,
                        const char *body, const char *content_type)
{
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
        esp_http_client_set_header(client, "Content-Type", content_type ? content_type : "text/plain");
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

    if (hb.truncated) {
        free(hb.data);
        return luaL_error(L, "HTTP response truncated (exceeded %d bytes)", HTTP_BUF_SIZE);
    }

    lua_pushstring(L, hb.data);
    lua_pushinteger(L, status);
    free(hb.data);
    return 2;  /* response, status */
}

/**
 * @brief HTTP GET request
 * socket.http.get(url) -> response, status
 */
static int socket_http_get(lua_State *L)
{
    const char *url = luaL_checkstring(L, 1);
    return http_request(L, "GET", url, NULL, NULL);
}

/**
 * @brief HTTP POST request
 * socket.http.post(url, body, content_type) -> response, status
 */
static int socket_http_post(lua_State *L)
{
    const char *url = luaL_checkstring(L, 1);
    const char *body = lua_isnoneornil(L, 2) ? NULL : luaL_checkstring(L, 2);
    const char *content_type = lua_isnoneornil(L, 3) ? "application/x-www-form-urlencoded" : luaL_optstring(L, 3, "application/x-www-form-urlencoded");
    return http_request(L, "POST", url, body, content_type);
}

/**
 * @brief HTTP request with method
 * socket.http.request(url, method, [body, content_type]) -> response, status
 *
 * LuaSocket-compatible API:
 * http.request(url)
 * http.request{method="POST", url=url, body=body}
 * http.request(method, url, body, headers)
 */
static int socket_http_request(lua_State *L)
{
    const char *url;
    const char *method;
    const char *body = NULL;
    const char *content_type = "application/x-www-form-urlencoded";

    /* Handle two calling conventions:
     * 1. http.request(url) or http.request(url, method)
     * 2. http.request{method=..., url=..., body=...}
     */

    if (lua_istable(L, 1)) {
        /* Table-based call: http.request{url=..., method=..., body=...} */
        lua_getfield(L, 1, "url");
        url = luaL_checkstring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "method");
        method = lua_isnoneornil(L, -1) ? "GET" : luaL_optstring(L, -1, "GET");
        lua_pop(L, 1);

        lua_getfield(L, 1, "body");
        body = lua_isnoneornil(L, -1) ? NULL : luaL_checkstring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "source");
        if (!lua_isnoneornil(L, -1)) {
            /* Source callback not supported */
            lua_pop(L, 1);
            return luaL_error(L, "source callback not supported");
        }
        lua_pop(L, 1);
    } else {
        /* Positional arguments: http.request(url, [method, body]) */
        url = luaL_checkstring(L, 1);
        method = lua_isnoneornil(L, 2) ? "GET" : luaL_optstring(L, 2, "GET");
        body = lua_isnoneornil(L, 3) ? NULL : luaL_checkstring(L, 3);

        if (lua_istable(L, 4)) {
            /* Headers table in position 4 */
            lua_getfield(L, 4, "Content-Type");
            if (!lua_isnoneornil(L, -1)) {
                content_type = lua_tostring(L, -1);
            }
            lua_pop(L, 1);
        }
    }

    return http_request(L, method, url, body, content_type);
}

/**
 * @brief Create socket table with http subtable
 * Creates: socket = {http = {request, get, post, ...}}
 */
static int luaopen_socket_socket(lua_State *L)
{
    /* Create socket table */
    lua_newtable(L);

    /* Create socket.http table */
    lua_newtable(L);

    /* Register functions in socket.http */
    lua_pushcfunction(L, socket_http_request);
    lua_setfield(L, -2, "request");

    lua_pushcfunction(L, socket_http_get);
    lua_setfield(L, -2, "get");

    lua_pushcfunction(L, socket_http_post);
    lua_setfield(L, -2, "post");

    /* Also add PUT and DELETE for convenience */
    lua_pushcfunction(L, socket_http_request);
    lua_setfield(L, -2, "put");

    lua_pushcfunction(L, socket_http_request);
    lua_setfield(L, -2, "delete");

    /* Set socket.http as field of socket */
    lua_setfield(L, -2, "http");

    return 1;
}

/**
 * @brief socket module entry point
 * Returns socket table with http subtable
 */
int luaopen_socket(lua_State *L)
{
    return luaopen_socket_socket(L);
}
