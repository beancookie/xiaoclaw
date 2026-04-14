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

/* Current tool index for dispatch */
static int s_current_tool_index = 0;

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

    atomic_fetch_sub(&ctx->pending_responses, 1);
    if (ctx->resp_sem) {
        xSemaphoreGive(ctx->resp_sem);
    }

    // Parse failure: resp_json is NULL, error_code is esp_err_t
    if (resp_json == NULL) {
        ESP_LOGE(TAG, "[Parse Failure] Endpoint: %s, Error: %s",
                 ep_name ? ep_name : "NULL", esp_err_to_name(error_code));
        return ESP_OK;
    }

    // Protocol-level error: error_code is negative JSON-RPC error code
    if (error_code < 0) {
        ESP_LOGE(TAG, "[Protocol Error] Endpoint: %s, Code: %d, Message: %s",
                 ep_name ? ep_name : "NULL", error_code, resp_json);
        return ESP_OK;
    }

    // Application-level response
    if (error_code == 0) {
        ESP_LOGI(TAG, "[Success] Endpoint: %s, Response: %s",
                 ep_name ? ep_name : "NULL", resp_json);
    } else if (error_code == 1) {
        ESP_LOGW(TAG, "[Application Error] Endpoint: %s, Response: %s",
                 ep_name ? ep_name : "NULL", resp_json);
    } else {
        ESP_LOGW(TAG, "[Unexpected] Endpoint: %s, Error code: %d, Response: %s",
                 ep_name ? ep_name : "NULL", error_code, resp_json);
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
 * @brief Static buffer for capturing tool call responses
 * @note Used because the simplified callback doesn't store resp_json.
 *       The semaphore pattern ensures only one request is in-flight at a time.
 */
static char s_tool_resp_buf[2048];

/**
 * @brief Static buffer for capturing list_tools response
 */
static char s_list_resp_buf[4096];

/**
 * @brief Extract result content from JSON-RPC response
 *
 * Parses JSON-RPC response and extracts the content from result.content array.
 * Returns the extracted text in output buffer.
 *
 * @param jsonrpc_resp Raw JSON-RPC response string
 * @param output Buffer to store extracted result
 * @param output_size Size of output buffer
 * @return ESP_OK on success, ESP_FAIL on failure
 */
static esp_err_t extract_jsonrpc_result(const char *jsonrpc_resp,
                                         char *output, size_t output_size)
{
    if (!jsonrpc_resp || !output || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(jsonrpc_resp);
    if (!root) {
        ESP_LOGE(TAG, "extract_jsonrpc_result: failed to parse JSON");
        snprintf(output, output_size, "{\"error\": \"invalid JSON response\"}");
        return ESP_FAIL;
    }

    /* Check for JSON-RPC error */
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error && cJSON_IsObject(error)) {
        cJSON *err_code = cJSON_GetObjectItem(error, "code");
        cJSON *err_msg = cJSON_GetObjectItem(error, "message");
        int code = err_code ? err_code->valueint : -1;
        const char *msg = err_msg && cJSON_IsString(err_msg) ? err_msg->valuestring : "unknown";
        ESP_LOGW(TAG, "extract_jsonrpc_result: JSON-RPC error %d: %s", code, msg);
        snprintf(output, output_size, "{\"error\": \"MCP error %d: %s\"}", code, msg);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    /* Extract result.content */
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!result || !cJSON_IsObject(result)) {
        ESP_LOGE(TAG, "extract_jsonrpc_result: no result object");
        snprintf(output, output_size, "{\"error\": \"no result in response\"}");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *content = cJSON_GetObjectItem(result, "content");
    if (!content || !cJSON_IsArray(content)) {
        /* No content array, return the whole result as-is */
        char *result_str = cJSON_PrintUnformatted(result);
        if (result_str) {
            strncpy(output, result_str, output_size - 1);
            output[output_size - 1] = '\0';
            free(result_str);
        } else {
            snprintf(output, output_size, "{\"error\": \"failed to format result\"}");
        }
        cJSON_Delete(root);
        return ESP_OK;
    }

    /* Get first content item */
    cJSON *first_item = cJSON_GetArrayItem(content, 0);
    if (!first_item || !cJSON_IsObject(first_item)) {
        snprintf(output, output_size, "{\"error\": \"empty content\"}");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *text = cJSON_GetObjectItem(first_item, "text");
    if (text && cJSON_IsString(text)) {
        strncpy(output, text->valuestring, output_size - 1);
        output[output_size - 1] = '\0';
    } else {
        char *item_str = cJSON_PrintUnformatted(first_item);
        if (item_str) {
            strncpy(output, item_str, output_size - 1);
            output[output_size - 1] = '\0';
            free(item_str);
        } else {
            snprintf(output, output_size, "{\"error\": \"failed to extract content\"}");
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Callback for tool calls that need response capture
 */
static esp_err_t mcp_tool_resp_cb(int error_code, const char *ep_name,
                                   const char *resp_json, void *user_ctx)
{
    resp_sync_ctx_t *ctx = (resp_sync_ctx_t *)user_ctx;
    if (!ctx) {
        ESP_LOGW(TAG, "Tool response callback: user_ctx is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    atomic_fetch_sub(&ctx->pending_responses, 1);

    /* Capture response for tool calls */
    if (resp_json) {
        strncpy(s_tool_resp_buf, resp_json, sizeof(s_tool_resp_buf) - 1);
        s_tool_resp_buf[sizeof(s_tool_resp_buf) - 1] = '\0';
    } else {
        s_tool_resp_buf[0] = '\0';
    }

    /* Log based on error_code (same分级 as mcp_resp_cb) */
    if (resp_json == NULL) {
        ESP_LOGE(TAG, "[Parse Failure] Endpoint: %s, Error: %s",
                 ep_name ? ep_name : "NULL", esp_err_to_name(error_code));
    } else if (error_code < 0) {
        ESP_LOGE(TAG, "[Protocol Error] Endpoint: %s, Code: %d, Message: %s",
                 ep_name ? ep_name : "NULL", error_code, resp_json);
    } else if (error_code == 0) {
        ESP_LOGI(TAG, "[Success] Endpoint: %s, Response: %s",
                 ep_name ? ep_name : "NULL", resp_json);
    } else if (error_code == 1) {
        ESP_LOGW(TAG, "[Application Error] Endpoint: %s, Response: %s",
                 ep_name ? ep_name : "NULL", resp_json);
    } else {
        ESP_LOGW(TAG, "[Unexpected] Endpoint: %s, Error code: %d, Response: %s",
                 ep_name ? ep_name : "NULL", error_code, resp_json);
    }

    if (ctx->resp_sem) {
        xSemaphoreGive(ctx->resp_sem);
    }

    return ESP_OK;
}

/**
 * @brief Callback for list_tools that captures response
 */
static esp_err_t mcp_list_resp_cb(int error_code, const char *ep_name,
                                   const char *resp_json, void *user_ctx)
{
    resp_sync_ctx_t *ctx = (resp_sync_ctx_t *)user_ctx;
    if (!ctx) {
        ESP_LOGW(TAG, "List response callback: user_ctx is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    atomic_fetch_sub(&ctx->pending_responses, 1);

    /* Capture response for list_tools */
    if (resp_json) {
        strncpy(s_list_resp_buf, resp_json, sizeof(s_list_resp_buf) - 1);
        s_list_resp_buf[sizeof(s_list_resp_buf) - 1] = '\0';
    } else {
        s_list_resp_buf[0] = '\0';
    }

    /* Log based on error_code */
    if (resp_json == NULL) {
        ESP_LOGE(TAG, "[Parse Failure] Endpoint: %s, Error: %s",
                 ep_name ? ep_name : "NULL", esp_err_to_name(error_code));
    } else if (error_code < 0) {
        ESP_LOGE(TAG, "[Protocol Error] Endpoint: %s, Code: %d, Message: %s",
                 ep_name ? ep_name : "NULL", error_code, resp_json);
    } else if (error_code == 0) {
        ESP_LOGI(TAG, "[Success] Endpoint: %s, Response: %s",
                 ep_name ? ep_name : "NULL", resp_json);
    } else if (error_code == 1) {
        ESP_LOGW(TAG, "[Application Error] Endpoint: %s, Response: %s",
                 ep_name ? ep_name : "NULL", resp_json);
    } else {
        ESP_LOGW(TAG, "[Unexpected] Endpoint: %s, Error code: %d, Response: %s",
                 ep_name ? ep_name : "NULL", error_code, resp_json);
    }

    if (ctx->resp_sem) {
        xSemaphoreGive(ctx->resp_sem);
    }

    return ESP_OK;
}

/**
 * @brief Execute MCP tool by name
 */
static esp_err_t mcp_tool_execute(const char *tool_name, const char *input_json,
                                   char *output, size_t output_size)
{
    if (!s_connected || !s_mgr) {
        snprintf(output, output_size, "{\"error\": \"MCP client not initialized\"}");
        return ESP_ERR_INVALID_STATE;
    }

    int tool_index = -1;
    for (int i = 0; i < s_mcp_tool_count; i++) {
        if (strcmp(s_remote_names[i], tool_name) == 0) {
            tool_index = i;
            break;
        }
    }

    if (tool_index < 0) {
        snprintf(output, output_size, "{\"error\": \"unknown tool: %s\"}", tool_name);
        return ESP_ERR_NOT_FOUND;
    }

    resp_sync_ctx_t *ctx = create_sync_ctx();
    if (!ctx) {
        snprintf(output, output_size, "{\"error\": \"out of memory\"}");
        return ESP_ERR_NO_MEM;
    }

    esp_mcp_mgr_req_t req = {
        .ep_name = s_server_config.endpoint,
        .cb = mcp_tool_resp_cb,
        .user_ctx = ctx,
        .u.call.tool_name = tool_name,
        .u.call.args_json = input_json ? input_json : "{}",
    };

    atomic_fetch_add(&ctx->pending_responses, 1);
    ESP_ERROR_CHECK(esp_mcp_mgr_post_tools_call(s_mgr, &req));

    esp_err_t ret = wait_for_response(ctx, s_server_config.timeout_ms);

    if (ret == ESP_ERR_TIMEOUT) {
        snprintf(output, output_size, "{\"error\": \"timeout\"}");
    } else if (s_tool_resp_buf[0] != '\0') {
        strncpy(output, s_tool_resp_buf, output_size - 1);
        output[output_size - 1] = '\0';
    }

    free_sync_ctx(ctx);
    return ret;
}

/**
 * @brief Unified execute function for all MCP tools
 */
static esp_err_t mcp_tool_exec(const char *input, char *output, size_t size)
{
    if (!s_connected || !s_mgr) {
        snprintf(output, size, "{\"error\": \"MCP client not initialized\"}");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_current_tool_index < 0 || s_current_tool_index >= s_mcp_tool_count) {
        snprintf(output, size, "{\"error\": \"invalid tool index\"}");
        return ESP_ERR_INVALID_ARG;
    }

    const char *tool_name = s_remote_names[s_current_tool_index];
    return mcp_tool_execute(tool_name, input, output, size);
}

/* Generate execute function for each slot, each captures its index */
#define DEFINE_MCP_EXEC(N) \
    static esp_err_t mcp_tool_exec_##N(const char *input, char *output, size_t size) \
    { \
        s_current_tool_index = N; \
        return mcp_tool_exec(input, output, size); \
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
    esp_err_t err = esp_mcp_mgr_post_info_init(s_mgr, &req);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_mcp_mgr_post_info_init failed: %s", esp_err_to_name(err));
        free_sync_ctx(ctx);
        return err;
    }

    esp_err_t ret = wait_for_response(ctx, s_server_config.timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCP initialize failed: %d", ret);
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
        .cb = mcp_list_resp_cb,
        .user_ctx = ctx,
        .u.list.cursor = NULL,
    };

    atomic_fetch_add(&ctx->pending_responses, 1);
    esp_err_t err = esp_mcp_mgr_post_tools_list(s_mgr, &req);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_mcp_mgr_post_tools_list failed: %s", esp_err_to_name(err));
        free_sync_ctx(ctx);
        return err;
    }

    esp_err_t ret = wait_for_response(ctx, s_server_config.timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCP tools/list failed: %d", ret);
    } else if (s_list_resp_buf[0] != '\0') {
        ret = handle_tools_list(s_list_resp_buf);
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
    ESP_LOGI(TAG, "mcp_tool_connect called: input=%s", input_json ? input_json : "NULL");

    if (!input_json || strlen(input_json) == 0) {
        snprintf(output, output_size, "{\"error\": \"server_name required\"}");
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse server_name from input */
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        ESP_LOGW(TAG, "mcp_tool_connect: invalid JSON");
        snprintf(output, output_size, "{\"error\": \"invalid JSON\"}");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *server_name_json = cJSON_GetObjectItem(root, "server_name");
    if (!server_name_json || !cJSON_IsString(server_name_json)) {
        ESP_LOGW(TAG, "mcp_tool_connect: server_name not found or not string");
        cJSON_Delete(root);
        snprintf(output, output_size, "{\"error\": \"server_name string required\"}");
        return ESP_ERR_INVALID_ARG;
    }

    const char *server_name = server_name_json->valuestring;
    ESP_LOGI(TAG, "mcp_tool_connect: server_name=%s", server_name);

    char host[128] = {0};
    int port = 0;
    char endpoint[64] = {0};

    /* Get config from skill_loader */
    esp_err_t err = skill_loader_get_mcp_server_config(server_name,
                                                         host, sizeof(host),
                                                         &port, endpoint, sizeof(endpoint));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mcp_tool_connect: skill_loader_get_mcp_server_config failed: %s", esp_err_to_name(err));
        cJSON_Delete(root);
        snprintf(output, output_size, "{\"error\": \"server '%s' not found in mcp-servers.md\"}", server_name);
        return err;
    }

    ESP_LOGI(TAG, "mcp_tool_connect: config loaded - host=%s, port=%d, endpoint=%s", host, port, endpoint);

    /* Connect */
    err = mcp_do_connect(server_name, host, port, endpoint);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mcp_tool_connect: mcp_do_connect failed: %s", esp_err_to_name(err));
        cJSON_Delete(root);
        snprintf(output, output_size, "{\"error\": \"failed to connect: %d\"}", err);
        return err;
    }

    ESP_LOGI(TAG, "mcp_tool_connect: success - connected to %s with %d tools", server_name, s_mcp_tool_count);
    cJSON_Delete(root);
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

/**
 * @brief mcp_server.tools_call tool implementation
 * Input: {"name": "echo", "arguments": {"message": "Hello"}}
 *
 * This allows calling arbitrary remote MCP tools by name.
 * Unlike mcp_tool_execute(), this bypasses the local tool index check
 * and forwards the call directly to the remote MCP server.
 */
static esp_err_t mcp_tools_call_exec(const char *input_json, char *output, size_t output_size)
{
    if (!s_connected || !s_mgr) {
        snprintf(output, output_size, "{\"error\": \"MCP client not initialized\"}");
        return ESP_ERR_INVALID_STATE;
    }

    if (!input_json || strlen(input_json) == 0) {
        snprintf(output, output_size, "{\"error\": \"name and arguments required\"}");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "{\"error\": \"invalid JSON\"}");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *name_json = cJSON_GetObjectItem(root, "name");
    cJSON *args_json = cJSON_GetObjectItem(root, "arguments");

    if (!name_json || !cJSON_IsString(name_json)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "{\"error\": \"name string required\"}");
        return ESP_ERR_INVALID_ARG;
    }

    const char *tool_name = name_json->valuestring;
    const char *args_str = "{}";
    char *args_str_alloc = NULL;

    if (args_json) {
        if (cJSON_IsObject(args_json)) {
            args_str_alloc = cJSON_PrintUnformatted(args_json);
            if (args_str_alloc) {
                args_str = args_str_alloc;
            }
            ESP_LOGI(TAG, "mcp_tools_call_exec: args is object, serialized to: %s", args_str);
        } else if (cJSON_IsString(args_json)) {
            args_str = args_json->valuestring;
            ESP_LOGI(TAG, "mcp_tools_call_exec: args is string: %s", args_str);
        } else {
            ESP_LOGI(TAG, "mcp_tools_call_exec: args type=%d", args_json->type);
        }
    } else {
        ESP_LOGI(TAG, "mcp_tools_call_exec: args is NULL");
    }

    ESP_LOGI(TAG, "mcp_tools_call_exec: final tool_name=%s, args_str=%s", tool_name, args_str);

    /* Create sync context for direct call */
    resp_sync_ctx_t *ctx = create_sync_ctx();
    if (!ctx) {
        if (args_str_alloc) free(args_str_alloc);
        cJSON_Delete(root);
        snprintf(output, output_size, "{\"error\": \"out of memory\"}");
        return ESP_ERR_NO_MEM;
    }

    /* Verify args_str is valid JSON object before calling */
    cJSON *test_parse = cJSON_Parse(args_str);
    if (!test_parse) {
        ESP_LOGE(TAG, "mcp_tools_call_exec: args_str is not valid JSON: %s", args_str);
        free_sync_ctx(ctx);
        if (args_str_alloc) free(args_str_alloc);
        cJSON_Delete(root);
        snprintf(output, output_size, "{\"error\": \"invalid JSON in arguments: %s\"}", args_str);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsObject(test_parse)) {
        ESP_LOGE(TAG, "mcp_tools_call_exec: args_str is not a JSON object: %s", args_str);
        cJSON_Delete(test_parse);
        free_sync_ctx(ctx);
        if (args_str_alloc) free(args_str_alloc);
        cJSON_Delete(root);
        snprintf(output, output_size, "{\"error\": \"arguments must be a JSON object\"}");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_Delete(test_parse);
    ESP_LOGI(TAG, "mcp_tools_call_exec: args_str validated as JSON object");

    /* Direct call bypassing local tool index check */
    esp_mcp_mgr_req_t req = {
        .ep_name = s_server_config.endpoint,
        .cb = mcp_tool_resp_cb,
        .user_ctx = ctx,
        .u.call.tool_name = tool_name,
        .u.call.args_json = args_str,
    };

    ESP_LOGI(TAG, "mcp_tools_call_exec: calling esp_mcp_mgr_post_tools_call");
    atomic_fetch_add(&ctx->pending_responses, 1);
    esp_err_t err = esp_mcp_mgr_post_tools_call(s_mgr, &req);
    ESP_LOGI(TAG, "mcp_tools_call_exec: esp_mcp_mgr_post_tools_call returned %d", err);

    if (err != ESP_OK) {
        if (args_str_alloc) free(args_str_alloc);
        free_sync_ctx(ctx);
        cJSON_Delete(root);
        snprintf(output, output_size, "{\"error\": \"post failed: %d\"}", err);
        return err;
    }

    esp_err_t ret = wait_for_response(ctx, s_server_config.timeout_ms);

    if (ret == ESP_ERR_TIMEOUT) {
        snprintf(output, output_size, "{\"error\": \"timeout\"}");
    } else if (s_tool_resp_buf[0] != '\0') {
        /* Extract result content from JSON-RPC response */
        extract_jsonrpc_result(s_tool_resp_buf, output, output_size);
    }

    if (args_str_alloc) {
        free(args_str_alloc);
    }
    free_sync_ctx(ctx);
    cJSON_Delete(root);
    return ret;
}

/**
 * @brief mcp_server.tools_list tool implementation
 * Input: {} or {"cursor": ""}
 *
 * Convenience tool that calls the remote MCP server's tools/list method.
 */
static esp_err_t mcp_tools_list_exec(const char *input_json, char *output, size_t output_size)
{
    if (!s_connected || !s_mgr) {
        snprintf(output, output_size, "{\"error\": \"MCP client not initialized\"}");
        return ESP_ERR_INVALID_STATE;
    }

    char *cursor = NULL;
    if (input_json && strlen(input_json) > 0) {
        cJSON *root = cJSON_Parse(input_json);
        if (root) {
            cJSON *cursor_json = cJSON_GetObjectItem(root, "cursor");
            if (cursor_json && cJSON_IsString(cursor_json)) {
                cursor = cursor_json->valuestring;
            }
            cJSON_Delete(root);
        }
    }

    resp_sync_ctx_t *ctx = create_sync_ctx();
    if (!ctx) {
        snprintf(output, output_size, "{\"error\": \"out of memory\"}");
        return ESP_ERR_NO_MEM;
    }

    esp_mcp_mgr_req_t req = {
        .ep_name = s_server_config.endpoint,
        .cb = mcp_tool_resp_cb,
        .user_ctx = ctx,
        .u.list.cursor = cursor,
    };

    atomic_fetch_add(&ctx->pending_responses, 1);
    esp_err_t err = esp_mcp_mgr_post_tools_list(s_mgr, &req);

    if (err != ESP_OK) {
        free_sync_ctx(ctx);
        snprintf(output, output_size, "{\"error\": \"post failed: %d\"}", err);
        return err;
    }

    esp_err_t ret = wait_for_response(ctx, s_server_config.timeout_ms);

    if (ret == ESP_ERR_TIMEOUT) {
        snprintf(output, output_size, "{\"error\": \"timeout\"}");
    } else if (s_tool_resp_buf[0] != '\0') {
        strncpy(output, s_tool_resp_buf, output_size - 1);
        output[output_size - 1] = '\0';
    }

    free_sync_ctx(ctx);
    return ret;
}

esp_err_t tool_mcp_client_init(void)
{
    if (s_connected) {
        ESP_LOGW(TAG, "MCP client already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Registering MCP dynamic tools (mcp_connect, mcp_disconnect, mcp_server.tools_call, mcp_server.tools_list)");

    /* Register mcp_connect tool */
    mimi_tool_t connect_tool = {
        .name = "mcp_connect",
        .description = "Connect to an MCP server. Input: {\"server_name\": \"xxx\"}. Server must be defined in mcp-servers.md skill file.",
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"server_name\":{\"type\":\"string\"}},\"required\":[\"server_name\"]}",
        .execute = mcp_tool_connect,
        .concurrency_safe = false,  /* modifies network state */
        .prepare = NULL,
    };
    ESP_ERROR_CHECK(tool_registry_add(&connect_tool));

    /* Register mcp_disconnect tool */
    mimi_tool_t disconnect_tool = {
        .name = "mcp_disconnect",
        .description = "Disconnect from the currently connected MCP server.",
        .input_schema_json = "{\"type\":\"object\",\"properties\":{}}",
        .execute = mcp_tool_disconnect,
        .concurrency_safe = false,  /* modifies network state */
        .prepare = NULL,
    };
    ESP_ERROR_CHECK(tool_registry_add(&disconnect_tool));

    /* Register mcp_server.tools_call tool */
    mimi_tool_t tools_call_tool = {
        .name = "mcp_server.tools_call",
        .description = "Call a remote MCP tool by name. Input: {\"name\": \"tool_name\", \"arguments\": {}}. "
                       "Use mcp_server.tools_list to list available tools.",
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"arguments\":{\"type\":\"object\"}},\"required\":[\"name\"]}",
        .execute = mcp_tools_call_exec,
        .concurrency_safe = false,
        .prepare = NULL,
    };
    ESP_ERROR_CHECK(tool_registry_add(&tools_call_tool));

    /* Register mcp_server.tools_list tool */
    mimi_tool_t tools_list_tool = {
        .name = "mcp_server.tools_list",
        .description = "List all available tools on the remote MCP server. Input: {}.",
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"cursor\":{\"type\":\"string\"}},\"required\":[]}",
        .execute = mcp_tools_list_exec,
        .concurrency_safe = true,
        .prepare = NULL,
    };
    ESP_ERROR_CHECK(tool_registry_add(&tools_list_tool));

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
