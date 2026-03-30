/**
 * @file tool_mcp_client.c
 * @brief MCP Client tool - Bridge to remote MCP server tools
 *
 * This module enables mimi to call tools from a remote MCP server.
 * Configuration is loaded from a skill file at runtime.
 * Remote tools are discovered at startup and registered as local mimi_tool_t entries.
 * Tool calls are forwarded via HTTP to the remote MCP server.
 */

#include "tool_mcp_client.h"
#include "tool_registry.h"
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
static const char *MCP_SKILL_FILE = MIMI_SPIFFS_BASE "/skills/mcp-connection.md";

/* Default timeout if not specified in skill file */
#define DEFAULT_MCP_TIMEOUT_MS 10000

/**
 * @brief MCP connection configuration (loaded from skill file)
 */
typedef struct {
    char host[128];
    int port;
    char endpoint[64];
    int timeout_ms;
    bool enabled;
} mcp_config_t;

/* Global configuration (loaded from skill file) */
static mcp_config_t s_config = {
    .host = {0},
    .port = 8000,
    .endpoint = "mcp",
    .timeout_ms = DEFAULT_MCP_TIMEOUT_MS,
    .enabled = false,
};

/* Global state */
static esp_mcp_t *s_mcp = NULL;
static esp_mcp_mgr_handle_t s_mgr = NULL;
static bool s_initialized = false;

/* Remote tool mapping: local_name -> remote_name */
#define MAX_MCP_TOOLS 16
static char s_local_names[MAX_MCP_TOOLS][64];
static char s_remote_names[MAX_MCP_TOOLS][64];
static int s_mcp_tool_count = 0;

/* Forward declaration */
static esp_err_t mcp_resp_cb(int error_code, const char *ep_name,
                               const char *resp_json, void *user_ctx);

/**
 * @brief Trim leading and trailing whitespace
 */
static void trim_string(char *str)
{
    if (!str) return;

    /* Trim leading */
    char *start = str;
    while (*start == ' ' || *start == '\t') start++;

    /* Trim trailing */
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) {
        *end-- = '\0';
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

/**
 * @brief Parse a key: value or - key: value line
 */
static bool parse_line(const char *line, char *key, char *value)
{
    const char *colon = strchr(line, ':');
    if (!colon) return false;

    size_t key_len = colon - line;
    while (key_len > 0 && (line[key_len - 1] == ' ' || line[key_len - 1] == '\t')) {
        key_len--;
    }

    if (key_len == 0) return false;

    memcpy(key, line, key_len);
    key[key_len] = '\0';

    const char *val_start = colon + 1;
    while (*val_start == ' ' || *val_start == '\t') val_start++;

    strcpy(value, val_start);
    trim_string(value);

    return true;
}

/**
 * @brief Load MCP configuration from skill file
 *
 * Expected format:
 * # MCP Server Connection
 *
 * ## Connection
 * - host: 192.168.1.100
 * - port: 8000
 * - endpoint: mcp
 * - timeout_ms: 10000
 * - enabled: true
 */
static esp_err_t load_config_from_skill(mcp_config_t *config)
{
    FILE *f = fopen(MCP_SKILL_FILE, "r");
    if (!f) {
        ESP_LOGI(TAG, "MCP skill file not found: %s (MCP client disabled)", MCP_SKILL_FILE);
        config->enabled = false;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Loading MCP config from: %s", MCP_SKILL_FILE);

    char line[256];
    bool in_connection_section = false;
    char current_key[64] = {0};
    char current_value[128] = {0};

    while (fgets(line, sizeof(line), f)) {
        /* Check for section header */
        if (strncmp(line, "## Connection", 13) == 0) {
            in_connection_section = true;
            continue;
        }

        /* Exit connection section on next header or EOF */
        if (line[0] == '#' && line[1] == '#') {
            in_connection_section = false;
            continue;
        }

        /* Skip non-connection sections */
        if (!in_connection_section) continue;

        /* Skip empty lines */
        if (line[0] == '\n' || line[0] == '\r' || line[0] == ' ') continue;

        /* Parse key: value lines */
        if (parse_line(line, current_key, current_value)) {
            if (strcmp(current_key, "host") == 0) {
                strncpy(config->host, current_value, sizeof(config->host) - 1);
            } else if (strcmp(current_key, "port") == 0) {
                config->port = atoi(current_value);
            } else if (strcmp(current_key, "endpoint") == 0) {
                strncpy(config->endpoint, current_value, sizeof(config->endpoint) - 1);
            } else if (strcmp(current_key, "timeout_ms") == 0) {
                config->timeout_ms = atoi(current_value);
            } else if (strcmp(current_key, "enabled") == 0) {
                config->enabled = (strcmp(current_value, "true") == 0 ||
                                   strcmp(current_value, "1") == 0 ||
                                   strcmp(current_value, "yes") == 0);
            }
        }
    }

    fclose(f);

    ESP_LOGI(TAG, "MCP config loaded: enabled=%d, host=%s, port=%d, endpoint=%s, timeout=%d",
             config->enabled, config->host, config->port, config->endpoint, config->timeout_ms);

    return ESP_OK;
}

/**
 * @brief Create a semaphore-based sync context for a tool call
 */
static mcp_call_ctx_t *create_call_ctx(void)
{
    mcp_call_ctx_t *ctx = calloc(1, sizeof(mcp_call_ctx_t));
    if (ctx) {
        ctx->resp_sem = xSemaphoreCreateCounting(1, 0);
        if (ctx->resp_sem == NULL) {
            free(ctx);
            return NULL;
        }
    }
    return ctx;
}

/**
 * @brief Free a call context
 */
static void free_call_ctx(mcp_call_ctx_t *ctx)
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
static esp_err_t wait_for_response(mcp_call_ctx_t *ctx, int timeout_ms)
{
    if (xSemaphoreTake(ctx->resp_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

/**
 * @brief MCP response callback (called from MCP manager task)
 */
static esp_err_t mcp_resp_cb(int error_code, const char *ep_name,
                               const char *resp_json, void *user_ctx)
{
    mcp_call_ctx_t *ctx = (mcp_call_ctx_t *)user_ctx;
    if (!ctx) {
        ESP_LOGW(TAG, "Response callback: user_ctx is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ctx->error_code = error_code;
    if (resp_json) {
        ctx->resp_json = strdup(resp_json);
        ctx->resp_len = ctx->resp_json ? strlen(ctx->resp_json) : 0;
    }
    ctx->done = true;

    if (ctx->resp_sem) {
        xSemaphoreGive(ctx->resp_sem);
    }

    return ESP_OK;
}

/**
 * @brief Post a request and wait for response
 */
static esp_err_t post_and_wait(esp_mcp_mgr_handle_t mgr, esp_mcp_mgr_req_t *req,
                                mcp_call_ctx_t *ctx, int timeout_ms)
{
    ctx->done = false;
    ctx->error_code = 0;
    ctx->resp_json = NULL;
    ctx->resp_len = 0;

    ESP_ERROR_CHECK(esp_mcp_mgr_post(mgr, req));

    esp_err_t ret = wait_for_response(ctx, timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Timeout waiting for response");
        return ret;
    }

    if (ctx->error_code < 0) {
        ESP_LOGW(TAG, "Protocol error: code=%d, msg=%s",
                 ctx->error_code, ctx->resp_json ? ctx->resp_json : "N/A");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Execute wrapper for MCP tools (generic, uses tool_index)
 */
static esp_err_t mcp_tool_execute(const char *input_json, char *output,
                                    size_t output_size, int tool_index)
{
    if (!s_initialized || !s_mgr) {
        snprintf(output, output_size, "{\"error\": \"MCP client not initialized\"}");
        return ESP_ERR_INVALID_STATE;
    }

    if (tool_index < 0 || tool_index >= s_mcp_tool_count) {
        snprintf(output, output_size, "{\"error\": \"invalid tool index\"}");
        return ESP_ERR_INVALID_ARG;
    }

    mcp_call_ctx_t *ctx = create_call_ctx();
    if (!ctx) {
        snprintf(output, output_size, "{\"error\": \"out of memory\"}");
        return ESP_ERR_NO_MEM;
    }

    esp_mcp_mgr_req_t req = {
        .ep_name = s_config.endpoint,
        .cb = mcp_resp_cb,
        .user_ctx = ctx,
        .u.call.tool_name = s_remote_names[tool_index],
        .u.call.args_json = input_json ? input_json : "{}",
    };

    esp_err_t ret = post_and_wait(s_mgr, &req, ctx, s_config.timeout_ms);

    if (ret == ESP_ERR_TIMEOUT) {
        snprintf(output, output_size, "{\"error\": \"timeout\"}");
    } else if (ret != ESP_OK) {
        snprintf(output, output_size, "{\"error\": \"protocol error: %d\"}", ctx->error_code);
    } else if (ctx->resp_json) {
        strncpy(output, ctx->resp_json, output_size - 1);
        output[output_size - 1] = '\0';
    }

    free_call_ctx(ctx);
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
        char local_name[64];
        snprintf(local_name, sizeof(local_name), "%s.%s",
                 s_config.endpoint, name->valuestring);

        /* Build schema JSON */
        char schema_json[512] = "{\"type\":\"object\",\"properties\":{}}";
        if (input_schema) {
            cJSON_PrintUnformatted(input_schema, schema_json, sizeof(schema_json) - 1);
            schema_json[sizeof(schema_json) - 1] = '\0';
        }

        /* Store mapping */
        strncpy(s_local_names[count], local_name, sizeof(s_local_names[count]) - 1);
        strncpy(s_remote_names[count], name->valuestring, sizeof(s_remote_names[count]) - 1);

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
    mcp_call_ctx_t *ctx = create_call_ctx();
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    esp_mcp_mgr_req_t req = {
        .ep_name = s_config.endpoint,
        .cb = mcp_resp_cb,
        .user_ctx = ctx,
        .u.init = {
            .protocol_version = "2024-11-05",
            .name = "mimi",
            .version = "1.0.0",
        },
    };

    esp_err_t ret = post_and_wait(s_mgr, &req, ctx, s_config.timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCP initialize failed: %d", ret);
    } else {
        ESP_LOGI(TAG, "MCP initialize success");
    }

    free_call_ctx(ctx);
    return ret;
}

/**
 * @brief Query tools/list from MCP server
 */
static esp_err_t mcp_list_tools(void)
{
    mcp_call_ctx_t *ctx = create_call_ctx();
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    esp_mcp_mgr_req_t req = {
        .ep_name = s_config.endpoint,
        .cb = mcp_resp_cb,
        .user_ctx = ctx,
        .u.list.cursor = NULL,
    };

    esp_err_t ret = post_and_wait(s_mgr, &req, ctx, s_config.timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCP tools/list failed: %d", ret);
    } else if (ctx->resp_json) {
        ret = handle_tools_list(ctx->resp_json);
    }

    free_call_ctx(ctx);
    return ret;
}

esp_err_t tool_mcp_client_init(void)
{
    /* Load configuration from skill file */
    ESP_ERROR_CHECK(load_config_from_skill(&s_config));

    if (!s_config.enabled) {
        ESP_LOGI(TAG, "MCP client disabled (enabled=false in skill file)");
        return ESP_OK;
    }

    if (s_initialized) {
        ESP_LOGW(TAG, "MCP client already initialized");
        return ESP_OK;
    }

    if (s_config.host[0] == '\0') {
        ESP_LOGW(TAG, "MCP client enabled but host is empty - skipping");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MCP client...");
    ESP_LOGI(TAG, "  Server: %s:%d", s_config.host, s_config.port);
    ESP_LOGI(TAG, "  Endpoint: %s", s_config.endpoint);

    /* Create MCP instance */
    ESP_ERROR_CHECK(esp_mcp_create(&s_mcp));

    /* Build base URL */
    char base_url[256];
    snprintf(base_url, sizeof(base_url), "http://%s:%d",
             s_config.host, s_config.port);

    /* Configure HTTP transport */
    esp_http_client_config_t httpc_cfg = {
        .url = base_url,
        .timeout_ms = s_config.timeout_ms,
    };

    /* Initialize MCP manager */
    esp_mcp_mgr_config_t mgr_cfg = {
        .transport = esp_mcp_transport_http_client,
        .config = &httpc_cfg,
        .instance = s_mcp,
    };

    ESP_ERROR_CHECK(esp_mcp_mgr_init(mgr_cfg, &s_mgr));
    ESP_ERROR_CHECK(esp_mcp_mgr_start(s_mgr));
    ESP_ERROR_CHECK(esp_mcp_mgr_register_endpoint(s_mgr, s_config.endpoint, NULL));

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

    s_initialized = true;
    ESP_LOGI(TAG, "MCP client initialized (%d tools)", s_mcp_tool_count);
    return ESP_OK;
}

esp_err_t tool_mcp_client_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_mgr) {
        esp_mcp_mgr_stop(s_mgr);
        esp_mcp_mgr_deinit(s_mgr);
        s_mgr = NULL;
    }

    if (s_mcp) {
        esp_mcp_destroy(s_mcp);
        s_mcp = NULL;
    }

    s_initialized = false;
    s_mcp_tool_count = 0;

    ESP_LOGI(TAG, "MCP client deinitialized");
    return ESP_OK;
}

bool tool_mcp_client_is_ready(void)
{
    return s_initialized && s_mgr != NULL;
}
