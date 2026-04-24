#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Initialize memory store. Ensures FATFS directories exist.
 */
esp_err_t memory_store_init(void);

/**
 * Read long-term memory (MEMORY.md) into buffer.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if file missing
 */
esp_err_t memory_read_long_term(char *buf, size_t size);

/**
 * Write content to long-term memory (MEMORY.md).
 */
esp_err_t memory_write_long_term(const char *content);

/**
 * Get user facts/preferences (L2 memory layer).
 * Tries facts.json first, then USER.md as fallback.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no facts file
 */
esp_err_t memory_get_facts(char *buf, size_t size);
