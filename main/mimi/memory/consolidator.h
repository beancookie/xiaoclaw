#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Consolidator configuration.
 */
typedef struct {
    int max_history;        /**< Maximum messages to keep in session before consolidating */
    int consolidate_batch;   /**< Number of messages to consolidate at once */
    int archive_max_lines;  /**< Maximum lines in archive file */
} consolidator_config_t;

/**
 * Default consolidator configuration.
 */
#define CONSOLIDATOR_CONFIG_DEFAULT { \
    .max_history = 50, \
    .consolidate_batch = 20, \
    .archive_max_lines = 500 \
}

/**
 * Initialize consolidator with configuration.
 * @param config Configuration (use CONSOLIDATOR_CONFIG_DEFAULT for defaults)
 */
esp_err_t consolidator_init(consolidator_config_t *config);

/**
 * Check if session needs consolidation and run if needed.
 * This is called before each LLM request.
 *
 * @param chat_id Session identifier
 * @return ESP_OK if no consolidation needed or consolidation complete
 */
esp_err_t consolidator_check_and_run(const char *chat_id);

/**
 * Force consolidation for a session (e.g., on low memory).
 * @param chat_id Session identifier
 * @return ESP_OK on success
 */
esp_err_t consolidator_force_run(const char *chat_id);

/**
 * Get current consolidator statistics.
 * @param total_sessions Output: number of sessions tracked
 * @param consolidated  Output: number of sessions that needed consolidation
 */
void consolidator_get_stats(int *total_sessions, int *consolidated);
