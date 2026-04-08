#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Build the system prompt from bootstrap files (SOUL.md, USER.md)
 * and memory context (MEMORY.md + recent daily notes).
 *
 * @param buf   Output buffer (caller allocates, recommend MIMI_CONTEXT_BUF_SIZE)
 * @param size  Buffer size
 */
esp_err_t context_build_system_prompt(char *buf, size_t size);

/**
 * Build runtime context string with current time and channel info.
 * This is injected before the user message to provide turn-specific metadata.
 *
 * @param buf      Output buffer
 * @param size     Buffer size
 * @param channel  Source channel (e.g. "xiaozhi", "telegram"), can be NULL
 * @param chat_id  Chat identifier, can be NULL
 * @return Number of bytes written
 */
size_t context_build_runtime_context(char *buf, size_t size, const char *channel, const char *chat_id);

/**
 * Build the complete message list for an LLM call.
 *
 * This combines:
 * - System message with system prompt
 * - Session history
 * - Runtime context (time, channel, chat_id) injected before current message
 * - Current user message
 *
 * @param history         JSON array string of previous messages, or NULL/empty
 * @param history_size    Size of history buffer
 * @param current_message Current user message
 * @param channel         Source channel (e.g. "xiaozhi", "telegram")
 * @param chat_id         Chat identifier
 * @param output_buf      Output buffer for the built messages JSON
 * @param output_size     Size of output buffer
 * @return ESP_OK on success
 */
esp_err_t context_build_messages(const char *history, size_t history_size,
                                 const char *current_message,
                                 const char *channel, const char *chat_id,
                                 char *output_buf, size_t output_size);
