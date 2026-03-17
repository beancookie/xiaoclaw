#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bridge layer between xiaozhi (I/O) and mimiclaw (Agent)
 *
 * This module provides bidirectional communication:
 * - xiaozhi -> Agent: via bridge_send_to_agent()
 * - Agent -> xiaozhi: via callback registered with bridge_set_response_callback()
 */

/**
 * @brief Initialize the bridge layer
 *
 * Must be called after message_bus is initialized (i.e., after mimiclaw_init())
 *
 * @return ESP_OK on success
 */
esp_err_t bridge_init(void);

/**
 * @brief Send a message from xiaozhi to the Agent
 *
 * The message will be pushed to the inbound message bus queue,
 * and will be processed by the Agent Loop.
 *
 * @param text The text message to send (null-terminated string)
 * @return ESP_OK on success, ESP_ERR_NO_MEM if queue is full
 */
esp_err_t bridge_send_to_agent(const char *text);

/**
 * @brief Bridge task that monitors outbound messages from Agent
 *
 * This task runs continuously, popping messages from the outbound queue
 * and calling the registered callback for messages destined to xiaozhi.
 *
 * @param arg Unused (can be NULL)
 */
void bridge_task(void *arg);

/**
 * @brief Callback type for Agent responses
 *
 * @param text The response text from the Agent
 */
typedef void (*bridge_response_cb_t)(const char *text);

/**
 * @brief Register a callback to receive Agent responses
 *
 * The callback will be invoked when a message from the Agent
 * is received on the outbound queue with channel "xiaozhi".
 *
 * @param callback The callback function, or NULL to unregister
 */
void bridge_set_response_callback(bridge_response_cb_t callback);

/**
 * @brief Start the bridge task
 *
 * Creates and starts the bridge task on Core 0.
 *
 * @return ESP_OK on success
 */
esp_err_t bridge_start(void);

#ifdef __cplusplus
}
#endif
