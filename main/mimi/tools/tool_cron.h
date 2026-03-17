#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Add a scheduled cron job.
 * Input JSON: { name, schedule_type ("every"/"at"), interval_s, at_epoch, message, channel?, chat_id? }
 */
esp_err_t tool_cron_add_execute(const char *input_json, char *output, size_t output_size);

/**
 * List all scheduled cron jobs.
 * Input JSON: {} (no required fields)
 */
esp_err_t tool_cron_list_execute(const char *input_json, char *output, size_t output_size);

/**
 * Remove a scheduled cron job by ID.
 * Input JSON: { job_id }
 */
esp_err_t tool_cron_remove_execute(const char *input_json, char *output, size_t output_size);
