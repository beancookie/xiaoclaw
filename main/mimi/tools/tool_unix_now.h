#pragma once

#include "esp_err.h"

/**
 * @brief Execute unix_now tool
 * @param input_json JSON input (unused, no parameters required)
 * @param output Output buffer for unix timestamp string
 * @param output_size Size of output buffer
 * @return ESP_OK on success
 */
esp_err_t tool_unix_now_execute(const char *input_json, char *output, size_t output_size);
