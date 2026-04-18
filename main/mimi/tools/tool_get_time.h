#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Execute get_datetime tool.
 * Fetches current time via HTTP Date header, sets system clock, returns formatted datetime string.
 */
esp_err_t tool_get_datetime_execute(const char *input_json, char *output, size_t output_size);

/**
 * Execute get_unix_timestamp tool.
 * Returns the current Unix timestamp in seconds.
 */
esp_err_t tool_get_unix_timestamp_execute(const char *input_json, char *output, size_t output_size);
