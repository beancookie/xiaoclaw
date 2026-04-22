#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * Context for skill crystallization.
 * Passed to skill_crystallize_if_needed() after task completion.
 */
typedef struct {
    bool last_task_success;           /**< true if task succeeded */
    int step_count;                   /**< Number of tool calls made */
    bool is_repetitive;               /**< true if similar task done before */
    const char *user_intent;          /**< User's original intent text */
    const char *tool_sequence_json;   /**< JSON array of {tool, input} calls */
    int sequence_len;                 /**< Number of tools in sequence */
} crystallize_context_t;

/**
 * Check if a successful multi-step task should be crystallized into an auto-skill.
 *
 * Conditions for crystallization:
 * - last_task_success == true
 * - step_count > 1
 * - is_repetitive OR step_count > 3
 * - No similar auto-skill already exists
 *
 * @param ctx  Crystallization context
 * @return true if crystallization should proceed
 */
bool skill_crystallize_should_create(const crystallize_context_t *ctx);

/**
 * Main entry point for skill crystallization.
 * Called after task completion if conditions are met.
 *
 * @param ctx  Crystallization context from task
 * @return ESP_OK if skill was created, ESP_ERR_NOT_FOUND if conditions not met
 */
esp_err_t skill_crystallize_if_needed(const crystallize_context_t *ctx);

/**
 * Generate a unique skill name from user intent.
 * Format: auto_{intent_hash}_{timestamp}
 *
 * @param intent  User intent text
 * @param buf     Output buffer
 * @param size    Buffer size
 */
void skill_crystallize_generate_name(const char *intent, char *buf, size_t size);

/**
 * Create an auto-skill file from tool sequence.
 *
 * @param name        Skill name (e.g., "auto_light_ctrl_a3f2_7d2e")
 * @param intent      User intent description
 * @param tool_seq    JSON array of tool calls
 * @param seq_len     Number of tools in sequence
 * @return ESP_OK on success
 */
esp_err_t skill_crystallize_create(const char *name, const char *intent,
                                     const char *tool_seq, int seq_len);

/**
 * Initialize skill crystallization subsystem.
 * No persistent state, so this is a no-op.
 *
 * @return ESP_OK
 */
esp_err_t skill_crystallize_init(void);