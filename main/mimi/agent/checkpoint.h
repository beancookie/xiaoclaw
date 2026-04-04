#pragma once

#include "esp_err.h"
#include "cJSON.h"
#include <stddef.h>

/**
 * Checkpoint phases
 */
typedef enum {
    CHECKPOINT_PHASE_STARTED,        /**< Turn started */
    CHECKPOINT_PHASE_AWAITING_TOOLS,  /**< Waiting for tool execution */
    CHECKPOINT_PHASE_TOOLS_DONE,      /**< Tools completed */
    CHECKPOINT_PHASE_FINAL_RESPONSE   /**< Final response generated */
} checkpoint_phase_t;

/**
 * Save a checkpoint for crash recovery.
 *
 * @param chat_id      Session chat_id
 * @param phase        Current checkpoint phase
 * @param iteration    Current iteration number
 * @param checkpoint   JSON object with checkpoint data
 */
esp_err_t checkpoint_save(const char *chat_id, checkpoint_phase_t phase,
                         int iteration, const cJSON *checkpoint);

/**
 * Load the latest checkpoint for a session.
 *
 * @param chat_id    Session chat_id
 * @param phase      Output: checkpoint phase
 * @param iteration  Output: iteration number
 * @param checkpoint Output: JSON object with checkpoint data (caller frees with cJSON_Delete)
 * @return ESP_OK if checkpoint found, ESP_ERR_NOT_FOUND if no checkpoint
 */
esp_err_t checkpoint_load(const char *chat_id, checkpoint_phase_t *phase,
                         int *iteration, cJSON **checkpoint);

/**
 * Clear checkpoint for a session (call after turn completes).
 */
esp_err_t checkpoint_clear(const char *chat_id);

/**
 * Check if a session has a pending checkpoint.
 */
bool checkpoint_exists(const char *chat_id);
