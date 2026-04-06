#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * Session metadata structure.
 */
typedef struct {
    char key[128];              /**< Session key (channel:chat_id) */
    int last_consolidated;      /**< Number of messages already consolidated */
    int total_messages;         /**< Total messages in session */
    int cursor;                 /**< Current history cursor position */
    time_t created_at;         /**< Session creation time */
    time_t updated_at;         /**< Last update time */
} session_metadata_t;

/**
 * Session message structure (for internal use).
 */
typedef struct {
    char role[16];              /**< "user" or "assistant" */
    char *content;              /**< Message content (heap allocated) */
    time_t timestamp;          /**< Message timestamp */
} session_message_t;

/**
 * Initialize session manager.
 */
esp_err_t session_manager_init(void);

/**
 * Append a message to a session file (JSONL format).
 * @param chat_id   Session identifier (e.g., "12345")
 * @param role      "user" or "assistant"
 * @param content   Message text
 */
esp_err_t session_append(const char *chat_id, const char *role, const char *content);

/**
 * Load session history as a JSON array string suitable for LLM messages.
 * Returns the last max_msgs messages as:
 * [{"role":"user","content":"..."},{"role":"assistant","content":"..."},...]
 *
 * @param chat_id   Session identifier
 * @param buf       Output buffer (caller allocates)
 * @param size      Buffer size
 * @param max_msgs  Maximum number of messages to return
 */
esp_err_t session_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs);

/**
 * Get session metadata.
 * @param chat_id   Session identifier
 * @param meta      Output metadata (caller allocates)
 * @return ESP_OK on success
 */
esp_err_t session_get_metadata(const char *chat_id, session_metadata_t *meta);

/**
 * Get unconsolidated messages (after last_consolidated).
 * Returns messages as JSON array for LLM.
 *
 * @param chat_id    Session identifier
 * @param buf        Output buffer
 * @param size       Buffer size
 * @param remaining  Output: number of messages remaining after this read
 * @return ESP_OK on success
 */
esp_err_t session_get_unconsolidated(const char *chat_id, char *buf, size_t size, int *remaining);

/**
 * Mark messages as consolidated (advance last_consolidated).
 * @param chat_id   Session identifier
 * @param count     Number of messages to mark as consolidated
 */
esp_err_t session_mark_consolidated(const char *chat_id, int count);

/**
 * Read messages after a specific cursor position.
 * @param chat_id    Session identifier
 * @param cursor     Cursor position to read after
 * @param buf        Output buffer
 * @param size       Buffer size
 * @param next_cursor Output: next cursor position (0 if EOF)
 * @return ESP_OK on success
 */
esp_err_t session_read_after_cursor(const char *chat_id, int cursor,
                                   char *buf, size_t size, int *next_cursor);

/**
 * Advance the session cursor.
 * @param chat_id    Session identifier
 * @param new_cursor New cursor position
 */
esp_err_t session_advance_cursor(const char *chat_id, int new_cursor);

/**
 * Get total message count for a session.
 * @param chat_id   Session identifier
 * @return Message count, or -1 if error
 */
int session_get_message_count(const char *chat_id);

/**
 * Clear a session (delete the file).
 */
esp_err_t session_clear(const char *chat_id);

/**
 * List all session files (prints to log).
 */
void session_list(void);

/**
 * Get memory usage statistics.
 * @param cached_sessions Output: number of cached sessions
 * @param total_messages Output: total messages across all sessions
 */
void session_get_stats(int *cached_sessions, int *total_messages);
