#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * Tool execute function signature.
 * @param input_json  JSON string of tool input
 * @param output      Output buffer for result
 * @param output_size Size of output buffer
 * @return ESP_OK on success
 */
typedef esp_err_t (*mimi_tool_execute_t)(const char *input_json, char *output, size_t output_size);

/**
 * Tool prepare function - called before execution for validation/modification.
 * @param name        Tool name
 * @param input_json  JSON string of tool input (may be modified in place)
 * @return NULL if OK, error message string if validation failed (caller frees)
 */
typedef char* (*mimi_tool_prepare_t)(const char *name, char *input_json);

/**
 * Tool definition.
 */
typedef struct {
    const char *name;
    const char *description;
    const char *input_schema_json;  /**< JSON Schema string for input */
    mimi_tool_execute_t execute;   /**< Execute function */
    bool concurrency_safe;          /**< true if tool can run concurrently with others */
    mimi_tool_prepare_t prepare;   /**< Optional prepare function for validation */
} mimi_tool_t;

/**
 * Initialize tool registry and register all built-in tools.
 */
esp_err_t tool_registry_init(void);

/**
 * Get the pre-built tools JSON array string for the API request.
 * Returns NULL if no tools are registered.
 */
const char *tool_registry_get_tools_json(void);

/**
 * Execute a tool by name.
 *
 * @param name         Tool name (e.g. "web_search")
 * @param input_json   JSON string of tool input
 * @param output       Output buffer for tool result text
 * @param output_size  Size of output buffer
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if tool unknown
 */
esp_err_t tool_registry_execute(const char *name, const char *input_json,
                                char *output, size_t output_size);

/**
 * Get a tool by name.
 * @param name Tool name
 * @return Tool pointer or NULL if not found
 */
const mimi_tool_t *tool_registry_get(const char *name);

/**
 * Check if a tool is concurrency-safe.
 * @param name Tool name
 * @return true if tool can run concurrently
 */
bool tool_registry_is_concurrency_safe(const char *name);

/**
 * Execute tool with optional prepare hook.
 * If prepare returns non-NULL, execution is skipped and the error is returned.
 */
esp_err_t tool_registry_execute_prepared(const char *name, char *input_json,
                                         char *output, size_t output_size);
