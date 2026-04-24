#pragma once

#include <stddef.h>
#include "esp_err.h"

/**
 * @brief Execute a Lua script from FATFS file.
 *
 * @param input_json JSON string with "path" field pointing to Lua script
 * @param output Output buffer for result
 * @param output_size Size of output buffer
 * @return ESP_OK on success
 */
esp_err_t tool_lua_run_execute(const char *input_json, char *output, size_t output_size);

/**
 * @brief Evaluate (execute) a Lua code string directly.
 *
 * @param input_json JSON string with "code" field containing Lua code
 * @param output Output buffer for result
 * @param output_size Size of output buffer
 * @return ESP_OK on success
 */
esp_err_t tool_lua_eval_execute(const char *input_json, char *output, size_t output_size);
