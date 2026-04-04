#include "tool_registry.h"
#include "mimi_config.h"
#include "tools/tool_web_search.h"
#include "tools/tool_get_time.h"
#include "tools/tool_files.h"
#include "tools/tool_cron.h"
#include "tools/tool_gpio.h"
#include "tools/tool_lua.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "tools";

#define MAX_TOOLS 32

static mimi_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static char *s_tools_json = NULL;  /* cached JSON array string */

static void register_tool(const mimi_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        ESP_LOGE(TAG, "Tool registry full");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    ESP_LOGI(TAG, "Registered tool: %s (concurrency_safe=%d)", tool->name, tool->concurrency_safe);
}

static void build_tools_json(void);

esp_err_t tool_registry_add(const mimi_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        ESP_LOGE(TAG, "Tool registry full, cannot add: %s", tool->name);
        return ESP_ERR_NO_MEM;
    }
    s_tools[s_tool_count++] = *tool;
    ESP_LOGI(TAG, "Added tool: %s", tool->name);
    /* Note: Caller should call tool_registry_rebuild_json() after adding multiple tools */
    return ESP_OK;
}

void tool_registry_rebuild_json(void)
{
    build_tools_json();
}

static void build_tools_json(void)
{
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    free(s_tools_json);
    s_tools_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    ESP_LOGI(TAG, "Tools JSON built (%d tools)", s_tool_count);
}

esp_err_t tool_registry_init(void)
{
    s_tool_count = 0;

    /* Register web_search - network call, safe to batch */
    tool_web_search_init();
    {
        static mimi_tool_t ws = {
            .name = "web_search",
            .description = "Search the web for current information via Tavily (preferred) or Brave when configured.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"The search query\"}},"
                "\"required\":[\"query\"]}",
            .execute = tool_web_search_execute,
            .concurrency_safe = true,
            .prepare = NULL,
        };
        register_tool(&ws);
    }

    /* Register get_current_time - pure read, safe */
    {
        static mimi_tool_t gt = {
            .name = "get_current_time",
            .description = "Get the current date and time. Also sets the system clock. Call this when you need to know what time or date it is.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{},"
                "\"required\":[]}",
            .execute = tool_get_time_execute,
            .concurrency_safe = true,
            .prepare = NULL,
        };
        register_tool(&gt);
    }

    /* Register read_file - safe read, can batch */
    {
        static mimi_tool_t rf = {
            .name = "read_file",
            .description = "Read a file from SPIFFS storage. Path must start with " MIMI_SPIFFS_BASE "/.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"}},"
                "\"required\":[\"path\"]}",
            .execute = tool_read_file_execute,
            .concurrency_safe = true,
            .prepare = NULL,
        };
        register_tool(&rf);
    }

    /* Register write_file - NOT safe, modifies filesystem */
    {
        static mimi_tool_t wf = {
            .name = "write_file",
            .description = "Write or overwrite a file on SPIFFS storage. Path must start with " MIMI_SPIFFS_BASE "/.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"},"
                "\"content\":{\"type\":\"string\",\"description\":\"File content to write\"}},"
                "\"required\":[\"path\",\"content\"]}",
            .execute = tool_write_file_execute,
            .concurrency_safe = false,  /* modifies filesystem */
            .prepare = NULL,
        };
        register_tool(&wf);
    }

    /* Register edit_file - NOT safe, modifies filesystem */
    {
        static mimi_tool_t ef = {
            .name = "edit_file",
            .description = "Find and replace text in a file on SPIFFS. Replaces first occurrence of old_string with new_string.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Absolute path starting with " MIMI_SPIFFS_BASE "/\"},"
                "\"old_string\":{\"type\":\"string\",\"description\":\"Text to find\"},"
                "\"new_string\":{\"type\":\"string\",\"description\":\"Replacement text\"}},"
                "\"required\":[\"path\",\"old_string\",\"new_string\"]}",
            .execute = tool_edit_file_execute,
            .concurrency_safe = false,  /* modifies filesystem */
            .prepare = NULL,
        };
        register_tool(&ef);
    }

    /* Register list_dir - safe read */
    {
        static mimi_tool_t ld = {
            .name = "list_dir",
            .description = "List files on SPIFFS storage, optionally filtered by path prefix.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"prefix\":{\"type\":\"string\",\"description\":\"Optional path prefix filter, e.g. " MIMI_SPIFFS_BASE "/memory/\"}},"
                "\"required\":[]}",
            .execute = tool_list_dir_execute,
            .concurrency_safe = true,
            .prepare = NULL,
        };
        register_tool(&ld);
    }

    /* Register cron_add - NOT safe, modifies cron state */
    {
        static mimi_tool_t ca = {
            .name = "cron_add",
            .description = "Schedule a recurring or one-shot task. The message will trigger an agent turn when the job fires.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{"
                "\"name\":{\"type\":\"string\",\"description\":\"Short name for the job\"},"
                "\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' for recurring interval or 'at' for one-shot at a unix timestamp\"},"
                "\"interval_s\":{\"type\":\"integer\",\"description\":\"Interval in seconds (required for 'every')\"},"
                "\"at_epoch\":{\"type\":\"integer\",\"description\":\"Unix timestamp to fire at (required for 'at')\"},"
                "\"message\":{\"type\":\"string\",\"description\":\"Message to inject when the job fires, triggering an agent turn\"},"
                "\"channel\":{\"type\":\"string\",\"description\":\"Optional reply channel (e.g. 'telegram'). If omitted, current turn channel is used when available\"},"
                "\"chat_id\":{\"type\":\"string\",\"description\":\"Optional reply chat_id. Required when channel='telegram'. If omitted during a Telegram turn, current chat_id is used\"}"
                "},"
                "\"required\":[\"name\",\"schedule_type\",\"message\"]}",
            .execute = tool_cron_add_execute,
            .concurrency_safe = false,  /* modifies cron state */
            .prepare = NULL,
        };
        register_tool(&ca);
    }

    /* Register cron_list - safe read */
    {
        static mimi_tool_t cl = {
            .name = "cron_list",
            .description = "List all scheduled cron jobs with their status, schedule, and IDs.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{},"
                "\"required\":[]}",
            .execute = tool_cron_list_execute,
            .concurrency_safe = true,
            .prepare = NULL,
        };
        register_tool(&cl);
    }

    /* Register cron_remove - NOT safe, modifies cron state */
    {
        static mimi_tool_t cr = {
            .name = "cron_remove",
            .description = "Remove a scheduled cron job by its ID.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"The 8-character job ID to remove\"}},"
                "\"required\":[\"job_id\"]}",
            .execute = tool_cron_remove_execute,
            .concurrency_safe = false,  /* modifies cron state */
            .prepare = NULL,
        };
        register_tool(&cr);
    }

    /* Register GPIO tools */
    tool_gpio_init();

    /* gpio_write - NOT safe, hardware access */
    {
        static mimi_tool_t gw = {
            .name = "gpio_write",
            .description = "Set a GPIO pin HIGH or LOW. Controls LEDs, relays, and other digital outputs.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number\"},"
                "\"state\":{\"type\":\"integer\",\"description\":\"1 for HIGH, 0 for LOW\"}},"
                "\"required\":[\"pin\",\"state\"]}",
            .execute = tool_gpio_write_execute,
            .concurrency_safe = false,  /* hardware access */
            .prepare = NULL,
        };
        register_tool(&gw);
    }

    /* gpio_read - safe read */
    {
        static mimi_tool_t gr = {
            .name = "gpio_read",
            .description = "Read a GPIO pin state. Returns HIGH or LOW. Use for checking switches, sensors, and digital inputs.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin number\"}},"
                "\"required\":[\"pin\"]}",
            .execute = tool_gpio_read_execute,
            .concurrency_safe = true,
            .prepare = NULL,
        };
        register_tool(&gr);
    }

    /* gpio_read_all - safe read */
    {
        static mimi_tool_t ga = {
            .name = "gpio_read_all",
            .description = "Read all allowed GPIO pin states in a single call. Returns each pin's HIGH/LOW state.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{},"
                "\"required\":[]}",
            .execute = tool_gpio_read_all_execute,
            .concurrency_safe = true,
            .prepare = NULL,
        };
        register_tool(&ga);
    }

    /* Register Lua tools */
    mimi_tool_t le = {
        .name = "lua_eval",
        .description = "Evaluate and execute a Lua code string directly. Use this to run quick Lua snippets or test Lua code.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"code\":{\"type\":\"string\",\"description\":\"Lua code to execute\"}},"
            "\"required\":[\"code\"]}",
        .execute = tool_lua_eval_execute,
    };
    register_tool(&le);

    mimi_tool_t lr = {
        .name = "lua_run",
        .description = "Execute a Lua script stored in SPIFFS. Path must start with " MIMI_SPIFFS_BASE "/lua/.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Path to Lua script starting with " MIMI_SPIFFS_BASE "/lua/\"}},"
            "\"required\":[\"path\"]}",
        .execute = tool_lua_run_execute,
    };
    register_tool(&lr);

    build_tools_json();

    ESP_LOGI(TAG, "Tool registry initialized with %d tools", s_tool_count);
    return ESP_OK;
}

const char *tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

const mimi_tool_t *tool_registry_get(const char *name)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            return &s_tools[i];
        }
    }
    return NULL;
}

bool tool_registry_is_concurrency_safe(const char *name)
{
    const mimi_tool_t *tool = tool_registry_get(name);
    return tool ? tool->concurrency_safe : false;
}

esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size)
{
    const mimi_tool_t *tool = tool_registry_get(name);
    if (!tool) {
        ESP_LOGW(TAG, "Unknown tool: %s", name);
        snprintf(output, output_size, "Error: unknown tool '%s'", name);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Executing tool: %s", name);

    /* Run prepare hook if set */
    if (tool->prepare) {
        char *input_copy = strndup(input_json, 4096);
        if (!input_copy) {
            snprintf(output, output_size, "Error: memory allocation failed");
            return ESP_ERR_NO_MEM;
        }

        char *error = tool->prepare(name, input_copy);
        if (error) {
            ESP_LOGW(TAG, "Tool %s prepare failed: %s", name, error);
            snprintf(output, output_size, "Error: %s", error);
            free(error);
            free(input_copy);
            return ESP_ERR_INVALID_ARG;
        }

        esp_err_t err = tool->execute(input_copy, output, output_size);
        free(input_copy);
        return err;
    }

    return tool->execute(input_json, output, output_size);
}

esp_err_t tool_registry_execute_prepared(const char *name, char *input_json,
                                         char *output, size_t output_size)
{
    const mimi_tool_t *tool = tool_registry_get(name);
    if (!tool) {
        ESP_LOGW(TAG, "Unknown tool: %s", name);
        snprintf(output, output_size, "Error: unknown tool '%s'", name);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Executing tool (prepared): %s", name);

    /* Run prepare hook if set */
    if (tool->prepare) {
        char *error = tool->prepare(name, input_json);
        if (error) {
            ESP_LOGW(TAG, "Tool %s prepare failed: %s", name, error);
            snprintf(output, output_size, "Error: %s", error);
            free(error);
            return ESP_ERR_INVALID_ARG;
        }
    }

    return tool->execute(input_json, output, output_size);
}
