/**
 * @file tool_mcp_client.c
 * @brief MCP Client tool - Bridge to remote MCP server tools
 *
 * This module enables mimi to dynamically connect to MCP servers.
 * Server configurations are stored in mcp-servers.md skill file.
 * Use mcp_connect(server_name) to establish a connection.
 * Remote tools are discovered and registered as local mimi_tool_t entries.
 * Tool calls are forwarded via HTTP to the remote MCP server.
 */

#include "tool_mcp_client.h"
#include "tool_registry.h"
#include "skills/skill_loader.h"
#include "mimi_config.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_mcp_engine.h"
#include "esp_mcp_mgr.h"
#include "esp_http_client.h"

static const char *TAG = "mcp_client";

/* Default timeout for MCP operations */
#define DEFAULT_MCP_TIMEOUT_MS 10000

/**
 * @brief Response synchronization context
 */
typedef struct {
    SemaphoreHandle_t resp_sem;
    atomic_int pending_responses;
    int last_error_code;
    char *resp_json;
} resp_sync_ctx_t;

/**
 * @brief MCP connection state
 */
typedef struct {
    char host[128];
    int port;
    char endpoint[64];
    int timeout_ms;
    char server_name[64];
} mcp_server_config_t;

/* Current connected server config */
static mcp_server_config_t s_server_config = {
    .host = {0},
    .port = 8000,
    .endpoint = "mcp",
    .timeout_ms = DEFAULT_MCP_TIMEOUT_MS,
    .server_name = {0},
};

/* Global state */
static esp_mcp_t *s_mcp = NULL;
static esp_mcp_mgr_handle_t s_mgr = 0;
static bool s_connected = false;

/* Remote tool mapping: local_name -> remote_name */
#define MAX_MCP_TOOLS 16
static char s_local_names[MAX_MCP_TOOLS][128];
static char s_remote_names[MAX_MCP_TOOLS][64];
static int s_mcp_tool_count = 0;

/**
 * @brief MCP response callback (called from MCP manager task)
 */
static esp_err_t mcp_resp_cb(int error_code, const char *ep_name,
                              const char *resp_json, void *user_ctx)
{
    resp_sync_ctx_t *ctx = (resp_sync_ctx_t *)user_ctx;
    if (!ctx) {
        ESP_LOGW(TAG, "Response callback: user_ctx is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ctx->last_error_code = error_code;
    if (resp_json) {
        ctx->resp_json = strdup(resp_json);
    }

    atomic_fetch_sub(&ctx->pending_responses, 1);
    if (ctx->resp_sem) {
        xSemaphoreGive(ctx->resp_sem);
    }

    return ESP_OK;
}

/**
 * @brief Create a semaphore-based sync context for a tool call
 */
static resp_sync_ctx_t *create_sync_ctx(void)
{
    resp_sync_ctx_t *ctx = calloc(1, sizeof(resp_sync_ctx_t));
    if (ctx) {
        ctx->resp_sem = xSemaphoreCreateCounting(10, 0);
        if (ctx->resp_sem == NULL) {
            free(ctx);
            return NULL;
        }
        atomic_init(&ctx->pending_responses, 0);
    }
    return ctx;
}

/**
 * @brief Free a sync context
 */
static void free_sync_ctx(resp_sync_ctx_t *ctx)
{
    if (ctx) {
        if (ctx->resp_sem) {
            vSemaphoreDelete(ctx->resp_sem);
        }
        if (ctx->resp_json) {
            free(ctx->resp_json);
        }
        free(ctx);
    }
}

/**
 * @brief Wait for response with timeout
 */
static esp_err_t wait_for_response(resp_sync_ctx_t *ctx, int timeout_ms)
{
    TickType_t start_tick = xTaskGetTickCount();

    while (atomic_load(&ctx->pending_responses) > 0) {
        TickType_t elapsed_ticks = xTaskGetTickCount() - start_tick;
        int elapsed_ms = (int)(elapsed_ticks * portTICK_PERIOD_MS);
        if (elapsed_ms >= timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }
        int wait_ms = timeout_ms - elapsed_ms;
        int wait_ticks = (wait_ms > 100) ? pdMS_TO_TICKS(100) : pdMS_TO_TICKS(wait_ms);
        xSemaphoreTake(ctx->resp_sem, wait_ticks);
    }

    return ESP_OK;
}

/**
 * @brief Execute wrapper for MCP tools (generic, uses tool_index)
 */
static esp_err_t mcp_tool_execute(const char *input_json, char *output,
                                   size_t output_size, int tool_index)
{
    if (!s_connected || !s_mgr) {
        snprintf(output, output_size, "{\"error\": \"MCP client not initialized\"}");
        return ESP_ERR_INVALID_STATE;
    }

    if (tool_index < 0 || tool_index >= s_mcp_tool_count) {
        snprintf(output, output_size, "{\"error\": \"invalid tool index\"}");
        return ESP_ERR_INVALID_ARG;
    }

    resp_sync_ctx_t *ctx = create_sync_ctx();
    if (!ctx) {
        snprintf(output, output_size, "{\"error\": \"out of memory\"}");
        return ESP_ERR_NO_MEM;
    }

    esp_mcp_mgr_req_t req = {
        .ep_name = s_server_config.endpoint,
        .cb = mcp_resp_cb,
        .user_ctx = ctx,
        .u.call.tool_name = s_remote_names[tool_index],
        .u.call.args_json = input_json ? input_json : "{}",
    };

    atomic_fetch_add(&ctx->pending_responses, 1);
    ESP_ERROR_CHECK(esp_mcp_mgr_post_tools_call(s_mgr, &req));

    esp_err_t ret = wait_for_response(ctx, s_server_config.timeout_ms);

    if (ret == ESP_ERR_TIMEOUT) {
        snprintf(output, output_size, "{\"error\": \"timeout\"}");
    } else if (ctx->last_error_code < 0) {
        snprintf(output, output_size, "{\"error\": \"protocol error: %d\", \"message\": \"%s\"}",
                 ctx->last_error_code, ctx->resp_json ? ctx->resp_json : "");
    } else if (ctx->resp_json) {
        strncpy(output, ctx->resp_json, output_size - 1);
        output[output_size - 1] = '\0';
    }

    free_sync_ctx(ctx);
    return ret;
}

/* Generate execute functions for each slot (max 16) */
#define DEFINE_MCP_EXEC(N) \
    static esp_err_t mcp_tool_exec_##N(const char *input, char *output, size_t size) \
    { \
        return mcp_tool_execute(input, output, size, N); \
    }

DEFINE_MCP_EXEC(0)
DEFINE_MCP_EXEC(1)
DEFINE_MCP_EXEC(2)
DEFINE_MCP_EXEC(3)
DEFINE_MCP_EXEC(4)
DEFINE_MCP_EXEC(5)
DEFINE_MCP_EXEC(6)
DEFINE_MCP_EXEC(7)
DEFINE_MCP_EXEC(8)
DEFINE_MCP_EXEC(9)
DEFINE_MCP_EXEC(10)
DEFINE_MCP_EXEC(11)
DEFINE_MCP_EXEC(12)
DEFINE_MCP_EXEC(13)
DEFINE_MCP_EXEC(14)
DEFINE_MCP_EXEC(15)

static esp_err_t (*s_mcp_exec_funcs[MAX_MCP_TOOLS])(const char *, char *, size_t) = {
    mcp_tool_exec_0, mcp_tool_exec_1, mcp_tool_exec_2, mcp_tool_exec_3,
    mcp_tool_exec_4, mcp_tool_exec_5, mcp_tool_exec_6, mcp_tool_exec_7,
    mcp_tool_exec_8, mcp_tool_exec_9, mcp_tool_exec_10, mcp_tool_exec_11,
    mcp_tool_exec_12, mcp_tool_exec_13, mcp_tool_exec_14, mcp_tool_exec_15,
};

/**
 * @brief Parse tools/list response and register tools locally
 */
static esp_err_t handle_tools_list(const char *resp_json)
{
    cJSON *root = cJSON_Parse(resp_json);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse tools/list response");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *tools = cJSON_GetObjectItem(root, "tools");
    if (!tools || !cJSON_IsArray(tools)) {
        ESP_LOGE(TAG, "Invalid tools/list response format");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *tool_item;
    int count = 0;
    cJSON_ArrayForEach(tool_item, tools) {
        if (count >= MAX_MCP_TOOLS) {
            ESP_LOGW(TAG, "Too many remote tools, truncating to %d", MAX_MCP_TOOLS);
            break;
        }

        cJSON *name = cJSON_GetObjectItem(tool_item, "name");
        cJSON *desc = cJSON_GetObjectItem(tool_item, "description");
        cJSON *input_schema = cJSON_GetObjectItem(tool_item, "inputSchema");

        if (!name || !cJSON_IsString(name)) {
            continue;
        }

        /* Build local name with prefix */
        char local_name[128];
        snprintf(local_name, sizeof(local_name), "%s.%s",
                 s_server_config.endpoint, name->valuestring);

        /* Build schema JSON */
        char schema_json[512] = "{\"type\":\"object\",\"properties\":{}}";
        if (input_schema) {
            char *json_str = cJSON_PrintUnformatted(input_schema);
            if (json_str) {
                strncpy(schema_json, json_str, sizeof(schema_json) - 1);
                schema_json[sizeof(schema_json) - 1] = '\0';
                free(json_str);
            }
        }

        /* Store mapping */
        strncpy(s_local_names[count], local_name, sizeof(s_local_names[count]) - 1);
        s_local_names[count][sizeof(s_local_names[count]) - 1] = '\0';
        strncpy(s_remote_names[count], name->valuestring, sizeof(s_remote_names[count]) - 1);
        s_remote_names[count][sizeof(s_remote_names[count]) - 1] = '\0';

        /* Register as mimi_tool_t */
        mimi_tool_t tool = {
            .name = s_local_names[count],
            .description = desc && cJSON_IsString(desc) ? desc->valuestring : "",
            .input_schema_json = schema_json,
            .execute = s_mcp_exec_funcs[count],
        };

        esp_err_t err = tool_registry_add(&tool);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Registered MCP tool: %s -> %s", local_name, name->valuestring);
        } else {
            ESP_LOGW(TAG, "Failed to register MCP tool %s: %d", local_name, err);
        }

        count++;
    }

    s_mcp_tool_count = count;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "MCP client: registered %d remote tools", count);
    return ESP_OK;
}

/**
 * @brief Send initialize request to MCP server
 */
static esp_err_t mcp_initialize(void)
{
    resp_sync_ctx_t *ctx = create_sync_ctx();
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    esp_mcp_mgr_req_t req = {
        .ep_name = s_server_config.endpoint,
        .cb = mcp_resp_cb,
        .user_ctx = ctx,
        .u.init = {
            .protocol_version = "2024-11-05",
            .name = "mimi",
            .version = "1.0.0",
        },
    };

    atomic_fetch_add(&ctx->pending_responses, 1);
    ESP_ERROR_CHECK(esp_mcp_mgr_post_info_init(s_mgr, &req));

    esp_err_t ret = wait_for_response(ctx, s_server_config.timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCP initialize failed: %d", ret);
    } else if (ctx->resp_json) {
        ESP_LOGI(TAG, "MCP initialize response: %s", ctx->resp_json);
    }

    free_sync_ctx(ctx);
    return ret;
}

/**
 * @brief Query tools/list from MCP server
 */
static esp_err_t mcp_list_tools(void)
{
    resp_sync_ctx_t *ctx = create_sync_ctx();
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    esp_mcp_mgr_req_t req = {
        .ep_name = s_server_config.endpoint,
        .cb = mcp_resp_cb,
        .user_ctx = ctx,
        .u.list.cursor = NULL,
    };

    atomic_fetch_add(&ctx->pending_responses, 1);
    ESP_ERROR_CHECK(esp_mcp_mgr_post_tools_list(s_mgr, &req));

    esp_err_t ret = wait_for_response(ctx, s_server_config.timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCP tools/list failed: %d", ret);
    } else if (ctx->resp_json) {
        ret = handle_tools_list(ctx->resp_json);
    }

    free_sync_ctx(ctx);
    return ret;
}

/**
 * @brief Internal: Establish MCP connection to a server
 */
static esp_err_t mcp_do_connect(const char *server_name, const char *host, int port,
                                 const char *endpoint)
{
    /* Disconnect existing if any */
    if (s_connected) {
        tool_mcp_client_deinit();
    }

    ESP_LOGI(TAG, "Connecting to MCP server: %s at %s:%d/%s",
             server_name, host, port, endpoint);

    /* Store server info */
    strncpy(s_server_config.server_name, server_name, sizeof(s_server_config.server_name) - 1);
    s_server_config.server_name[sizeof(s_server_config.server_name) - 1] = '\0';
    strncpy(s_server_config.host, host, sizeof(s_server_config.host) - 1);
    s_server_config.host[sizeof(s_server_config.host) - 1] = '\0';
    s_server_config.port = port;
    strncpy(s_server_config.endpoint, endpoint, sizeof(s_server_config.endpoint) - 1);
    s_server_config.endpoint[sizeof(s_server_config.endpoint) - 1] = '\0';
    s_server_config.timeout_ms = DEFAULT_MCP_TIMEOUT_MS;

    /* Create MCP instance */
    ESP_ERROR_CHECK(esp_mcp_create(&s_mcp));

    /* Build base URL */
    char base_url[256];
    snprintf(base_url, sizeof(base_url), "http://%s:%d", host, port);

    /* Configure HTTP transport */
    esp_http_client_config_t httpc_cfg = {
        .url = base_url,
        .timeout_ms = s_server_config.timeout_ms,
    };

    /* Initialize MCP manager */
    esp_mcp_mgr_config_t mgr_cfg = {
        .transport = esp_mcp_transport_http_client,
        .config = &httpc_cfg,
        .instance = s_mcp,
    };

    s_mgr = 0;
    ESP_ERROR_CHECK(esp_mcp_mgr_init(mgr_cfg, &s_mgr));
    ESP_ERROR_CHECK(esp_mcp_mgr_start(s_mgr));
    ESP_ERROR_CHECK(esp_mcp_mgr_register_endpoint(s_mgr, endpoint, NULL));

    /* Send initialize */
    esp_err_t ret = mcp_initialize();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCP initialize warning, continuing...");
    }

    /* Query remote tools */
    ret = mcp_list_tools();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCP tools/list warning, continuing...");
    }

    /* Rebuild tools JSON to include newly added MCP tools */
    tool_registry_rebuild_json();

    s_connected = true;
    ESP_LOGI(TAG, "MCP client connected to %s (%d tools)", server_name, s_mcp_tool_count);
    return ESP_OK;
}

/**
 * @brief mcp_connect tool implementation
 * Input: {"server_name": "xxx"}
 */
static esp_err_t mcp_tool_connect(const char *input_json, char *output, size_t output_size)
{
    if (!input_json || strlen(input_json) == 0) {
        snprintf(output, output_size, "{\"error\": \"server_name required\"}");
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse server_name from input */
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "{\"error\": \"invalid JSON\"}");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *server_name_json = cJSON_GetObjectItem(root, "server_name");
    if (!server_name_json || !cJSON_IsString(server_name_json)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "{\"error\": \"server_name string required\"}");
        return ESP_ERR_INVALID_ARG;
    }

    const char *server_name = server_name_json->valuestring;
    char host[128] = {0};
    int port = 0;
    char endpoint[64] = {0};

    /* Get config from skill_loader */
    esp_err_t err = skill_loader_get_mcp_server_config(server_name,
                                                         host, sizeof(host),
                                                         &port, endpoint, sizeof(endpoint));
    cJSON_Delete(root);

    if (err != ESP_OK) {
        snprintf(output, output_size, "{\"error\": \"server '%s' not found in mcp-servers.md\"}", server_name);
        return err;
    }

    /* Connect */
    err = mcp_do_connect(server_name, host, port, endpoint);
    if (err != ESP_OK) {
        snprintf(output, output_size, "{\"error\": \"failed to connect: %d\"}", err);
        return err;
    }

    snprintf(output, output_size,
             "{\"connected\": true, \"server\": \"%s\", \"host\": \"%s\", \"port\": %d, \"tools\": %d}",
             server_name, host, port, s_mcp_tool_count);
    return ESP_OK;
}

/**
 * @brief mcp_disconnect tool implementation
 */
static esp_err_t mcp_tool_disconnect(const char *input_json, char *output, size_t output_size)
{
    (void)input_json; /* unused */

    if (!s_connected) {
        snprintf(output, output_size, "{\"connected\": false, \"message\": \"not connected\"}");
        return ESP_OK;
    }

    /* Save server name before deinit clears it */
    char server_name[64] = {0};
    strncpy(server_name, s_server_config.server_name, sizeof(server_name) - 1);

    tool_mcp_client_deinit();

    snprintf(output, output_size, "{\"connected\": false, \"disconnected\": \"%s\"}", server_name);
    return ESP_OK;
}

esp_err_t tool_mcp_client_init(void)
{
    if (s_connected) {
        ESP_LOGW(TAG, "MCP client already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Registering MCP dynamic tools (mcp_connect, mcp_disconnect)");

    /* Register mcp_connect tool */
    mimi_tool_t connect_tool = {
        .name = "mcp_connect",
        .description = "Connect to an MCP server. Input: {\"server_name\": \"xxx\"}. Server must be defined in mcp-servers.md skill file.",
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"server_name\":{\"type\":\"string\"}},\"required\":[\"server_name\"]}",
        .execute = mcp_tool_connect,
    };
    ESP_ERROR_CHECK(tool_registry_add(&connect_tool));

    /* Register mcp_disconnect tool */
    mimi_tool_t disconnect_tool = {
        .name = "mcp_disconnect",
        .description = "Disconnect from the currently connected MCP server.",
        .input_schema_json = "{\"type\":\"object\",\"properties\":{}}",
        .execute = mcp_tool_disconnect,
    };
    ESP_ERROR_CHECK(tool_registry_add(&disconnect_tool));

    tool_registry_rebuild_json();

    ESP_LOGI(TAG, "MCP dynamic tools registered");
    return ESP_OK;
}

esp_err_t tool_mcp_client_deinit(void)
{
    if (!s_connected) {
        return ESP_OK;
    }

    if (s_mgr) {
        esp_mcp_mgr_stop(s_mgr);
        esp_mcp_mgr_deinit(s_mgr);
        s_mgr = 0;
    }

    if (s_mcp) {
        esp_mcp_destroy(s_mcp);
        s_mcp = NULL;
    }

    /* Reset server config */
    memset(&s_server_config, 0, sizeof(s_server_config));
    s_server_config.port = 8000;
    strcpy(s_server_config.endpoint, "mcp");
    s_server_config.timeout_ms = DEFAULT_MCP_TIMEOUT_MS;

    s_connected = false;
    s_mcp_tool_count = 0;

    ESP_LOGI(TAG, "MCP client deinitialized");
    return ESP_OK;
}

bool tool_mcp_client_is_ready(void)
{
    return s_connected && s_mgr != 0;
}
