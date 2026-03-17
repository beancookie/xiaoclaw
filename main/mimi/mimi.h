/**
 * @file mimi.h
 * @brief Mimiclaw Agent Engine - Public API
 *
 * This header provides the public interface for the Mimiclaw Agent
 * engine when embedded into xiaozhi or other host applications.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Mimiclaw Agent engine
 *
 * Prerequisites:
 * - NVS flash initialized
 * - Default event loop created
 * - WiFi connected (for LLM API access)
 * - SPIFFS partition available
 *
 * This function:
 * - Initializes message bus for communication
 * - Initializes memory store and session manager
 * - Initializes LLM proxy for API calls
 * - Initializes tool registry
 * - Starts Agent Loop task on Core 1
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mimiclaw_init(void);

/**
 * @brief Start the Mimiclaw Agent engine (alias for mimiclaw_init)
 *
 * @return ESP_OK on success
 */
esp_err_t mimiclaw_start(void);

/**
 * @brief Check if Mimiclaw is ready to process messages
 *
 * @return true if Agent Loop is running and ready
 */
bool mimiclaw_is_ready(void);

#ifdef __cplusplus
}
#endif
