#include "session_manager.h"
#include "mimi_config.h"
#include "util/fatfs_util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "session";

/* Session cache limit */
#define SESSION_CACHE_MAX_SIZE 8

/* Session cache entry */
typedef struct {
    char session_key[128];
    session_metadata_t metadata;
    bool needs_save;  /* Dirty flag - needs to be persisted */
} session_cache_entry_t;

static session_cache_entry_t s_session_cache_entries[SESSION_CACHE_MAX_SIZE];
static int s_session_cache_count = 0;

/* ─── Helper: get session file path ─────────────────────────────────── */

void session_get_path(const char *chat_id, char *buf, size_t size)
{
    snprintf(buf, size, "%s/tg_%s.jsonl", MIMI_FATFS_SESSION_DIR, chat_id);
}

/* ─── Helper: get metadata file path ─────────────────────────────────── */

void metadata_get_path(const char *chat_id, char *buf, size_t size)
{
    snprintf(buf, size, "%s/tg_%s.meta", MIMI_FATFS_SESSION_DIR, chat_id);
}

/* ─── Helper: find or create cache entry ─────────────────────────────── */

static session_cache_entry_t *session_find_or_create_cache_entry(const char *session_key, bool create_if_not_found)
{
    /* Search existing entries */
    for (int i = 0; i < s_session_cache_count; i++) {
        if (strcmp(s_session_cache_entries[i].session_key, session_key) == 0) {
            return &s_session_cache_entries[i];
        }
    }

    /* Create new entry if space available */
    if (create_if_not_found && s_session_cache_count < SESSION_CACHE_MAX_SIZE) {
        memset(&s_session_cache_entries[s_session_cache_count], 0, sizeof(session_cache_entry_t));
        strncpy(s_session_cache_entries[s_session_cache_count].session_key, session_key,
                 sizeof(s_session_cache_entries[s_session_cache_count].session_key) - 1);
        s_session_cache_entries[s_session_cache_count].metadata.created_at = time(NULL);
        s_session_cache_entries[s_session_cache_count].metadata.updated_at = time(NULL);
        return &s_session_cache_entries[s_session_cache_count++];
    }

    /* LRU eviction: replace oldest (first) entry */
    if (create_if_not_found && s_session_cache_count > 0) {
        /* Shift all entries down */
        memmove(&s_session_cache_entries[0], &s_session_cache_entries[1],
                (s_session_cache_count - 1) * sizeof(session_cache_entry_t));
        memset(&s_session_cache_entries[s_session_cache_count - 1], 0, sizeof(session_cache_entry_t));
        strncpy(s_session_cache_entries[s_session_cache_count - 1].session_key, session_key,
                 sizeof(s_session_cache_entries[0].session_key) - 1);
        s_session_cache_entries[s_session_cache_count - 1].metadata.created_at = time(NULL);
        s_session_cache_entries[s_session_cache_count - 1].metadata.updated_at = time(NULL);
        return &s_session_cache_entries[s_session_cache_count - 1];
    }

    return NULL;
}

/* ─── Helper: count messages in session file ─────────────────────────── */

static int session_count_messages_in_file(const char *chat_id)
{
    char path[64];
    session_get_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    int count = 0;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == '\0') continue;
        count++;
    }
    fclose(f);
    return count;
}

/* ─── Helper: load metadata from file ─────────────────────────────────── */

static esp_err_t session_load_metadata_from_file(const char *chat_id, session_metadata_t *metadata)
{
    char path[64];
    metadata_get_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        /* No metadata file, return defaults */
        memset(metadata, 0, sizeof(*metadata));
        metadata->last_consolidated = 0;
        metadata->total_messages = session_count_messages_in_file(chat_id);
        metadata->cursor = 0;
        metadata->created_at = time(NULL);
        metadata->updated_at = time(NULL);
        return ESP_OK;
    }

    char line[256];
    if (fgets(line, sizeof(line), f)) {
        /* Format: last_consolidated:total_messages:cursor:created_at:updated_at */
        int lc, tm, cu;
        long ca, ua;
        if (sscanf(line, "%d:%d:%d:%ld:%ld", &lc, &tm, &cu, &ca, &ua) == 5) {
            metadata->last_consolidated = lc;
            metadata->total_messages = tm;
            metadata->cursor = cu;
            metadata->created_at = (time_t)ca;
            metadata->updated_at = (time_t)ua;
        } else {
            memset(metadata, 0, sizeof(*metadata));
        }
    } else {
        memset(metadata, 0, sizeof(*metadata));
    }
    fclose(f);

    return ESP_OK;
}

/* ─── Helper: save metadata to file ─────────────────────────────────── */

static esp_err_t session_save_metadata_to_file(const char *chat_id, session_metadata_t *metadata)
{
    char path[64];
    metadata_get_path(chat_id, path, sizeof(path));

    char buf[256];
    int len = snprintf(buf, sizeof(buf), "%d:%d:%d:%ld:%ld\n",
            metadata->last_consolidated,
            metadata->total_messages,
            metadata->cursor,
            (long)metadata->created_at,
            (long)metadata->updated_at);

    esp_err_t err = fatfs_write_atomic(path, buf, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to atomically write metadata file: %s", path);
        return err;
    }

    return ESP_OK;
}

/* ─── Public API ─────────────────────────────────────────────────────── */

esp_err_t session_manager_init(void)
{
    s_session_cache_count = 0;
    ESP_LOGI(TAG, "Session manager initialized (cache size: %d)", SESSION_CACHE_MAX_SIZE);
    return ESP_OK;
}

esp_err_t session_append(const char *chat_id, const char *role, const char *content)
{
    char path[64];
    session_get_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open session file %s", path);
        return ESP_FAIL;
    }

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "role", role);
    cJSON_AddStringToObject(obj, "content", content);
    cJSON_AddNumberToObject(obj, "ts", (double)time(NULL));

    char *line = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (line) {
        fprintf(f, "%s\n", line);
        free(line);
    }

    fclose(f);

    /* Update cache */
    session_cache_entry_t *entry = session_find_or_create_cache_entry(chat_id, true);
    if (entry) {
        entry->metadata.total_messages++;
        entry->metadata.updated_at = time(NULL);
        entry->needs_save = true;
    }

    return ESP_OK;
}

esp_err_t session_get_history_json(const char *chat_id, char *buf, size_t size, int max_msgs)
{
    char path[64];
    session_get_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(buf, size, "[]");
        return ESP_OK;
    }

    /* Read all messages into array */
    cJSON *messages[256];
    int count = 0;

    char line[4096];
    while (fgets(line, sizeof(line), f) && count < 256) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        messages[count++] = obj;
    }
    fclose(f);

    /* Build output with only last max_msgs */
    cJSON *arr = cJSON_CreateArray();
    int start = (count > max_msgs) ? (count - max_msgs) : 0;

    for (int i = start; i < count; i++) {
        cJSON *src = messages[i];
        cJSON *role = cJSON_GetObjectItem(src, "role");
        cJSON *content = cJSON_GetObjectItem(src, "content");

        if (role && content) {
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
            cJSON_AddItemToArray(arr, entry);
        }
        cJSON_Delete(src);
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[]");
    }

    return ESP_OK;
}

esp_err_t session_get_metadata(const char *chat_id, session_metadata_t *metadata)
{
    if (!metadata) return ESP_ERR_INVALID_ARG;

    /* Check cache first */
    session_cache_entry_t *entry = session_find_or_create_cache_entry(chat_id, false);
    if (entry) {
        *metadata = entry->metadata;
        return ESP_OK;
    }

    /* Load from file */
    return session_load_metadata_from_file(chat_id, metadata);
}

esp_err_t session_get_unconsolidated(const char *chat_id, char *buf, size_t size, int *remaining)
{
    session_metadata_t metadata;
    esp_err_t err = session_get_metadata(chat_id, &metadata);
    if (err != ESP_OK) {
        snprintf(buf, size, "[]");
        if (remaining) *remaining = 0;
        return err;
    }

    char path[64];
    session_get_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(buf, size, "[]");
        if (remaining) *remaining = 0;
        return ESP_OK;
    }

    /* Read messages and filter */
    cJSON *messages[256];
    int count = 0;

    char line[4096];
    while (fgets(line, sizeof(line), f) && count < 256) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        messages[count++] = obj;
    }
    fclose(f);

    /* Build output with only unconsolidated messages */
    cJSON *arr = cJSON_CreateArray();

    for (int i = metadata.last_consolidated; i < count; i++) {
        cJSON *src = messages[i];
        cJSON *role = cJSON_GetObjectItem(src, "role");
        cJSON *content = cJSON_GetObjectItem(src, "content");

        if (role && content) {
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
            cJSON_AddItemToArray(arr, entry);
        }
        cJSON_Delete(src);
    }

    if (remaining) {
        *remaining = metadata.total_messages - metadata.last_consolidated;
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[]");
    }

    return ESP_OK;
}

esp_err_t session_mark_consolidated(const char *chat_id, int count)
{
    session_metadata_t metadata;
    esp_err_t err = session_get_metadata(chat_id, &metadata);
    if (err != ESP_OK) return err;

    metadata.last_consolidated += count;
    if (metadata.last_consolidated > metadata.total_messages) {
        metadata.last_consolidated = metadata.total_messages;
    }
    metadata.updated_at = time(NULL);

    /* Update cache */
    session_cache_entry_t *entry = session_find_or_create_cache_entry(chat_id, true);
    if (entry) {
        entry->metadata = metadata;
        entry->needs_save = true;
    }

    return session_save_metadata_to_file(chat_id, &metadata);
}

esp_err_t session_read_after_cursor(const char *chat_id, int cursor,
                                   char *buf, size_t size, int *next_cursor)
{
    char path[64];
    session_get_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(buf, size, "[]");
        if (next_cursor) *next_cursor = 0;
        return ESP_OK;
    }

    /* Skip to cursor position */
    cJSON *messages[256];
    int message_count = 0;
    int position = 0;

    char line[4096];
    while (fgets(line, sizeof(line), f) && message_count < 256) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        cJSON *obj = cJSON_Parse(line);
        if (!obj) continue;

        if (position > cursor) {
            messages[message_count++] = obj;
        } else {
            cJSON_Delete(obj);
        }
        position++;
    }
    fclose(f);

    /* Build output */
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < message_count; i++) {
        cJSON *src = messages[i];
        cJSON *role = cJSON_GetObjectItem(src, "role");
        cJSON *content = cJSON_GetObjectItem(src, "content");

        if (role && content) {
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "role", role->valuestring);
            cJSON_AddStringToObject(entry, "content", content->valuestring);
            cJSON_AddItemToArray(arr, entry);
        }
        cJSON_Delete(src);
    }

    if (next_cursor) {
        *next_cursor = cursor + message_count;
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[]");
    }

    return ESP_OK;
}

esp_err_t session_advance_cursor(const char *chat_id, int new_cursor)
{
    session_metadata_t metadata;
    esp_err_t err = session_get_metadata(chat_id, &metadata);
    if (err != ESP_OK) return err;

    if (new_cursor > metadata.cursor) {
        metadata.cursor = new_cursor;
        metadata.updated_at = time(NULL);

        /* Update cache */
        session_cache_entry_t *entry = session_find_or_create_cache_entry(chat_id, true);
        if (entry) {
            entry->metadata = metadata;
            entry->needs_save = true;
        }

        return session_save_metadata_to_file(chat_id, &metadata);
    }

    return ESP_OK;
}

int session_get_message_count(const char *chat_id)
{
    session_metadata_t metadata;
    if (session_get_metadata(chat_id, &metadata) == ESP_OK) {
        return metadata.total_messages;
    }
    return 0;
}

esp_err_t session_clear(const char *chat_id)
{
    char path[64];
    session_get_path(chat_id, path, sizeof(path));

    if (remove(path) == 0) {
        /* Also remove metadata */
        metadata_get_path(chat_id, path, sizeof(path));
        remove(path);

        /* Remove from cache */
        for (int i = 0; i < s_session_cache_count; i++) {
            if (strcmp(s_session_cache_entries[i].session_key, chat_id) == 0) {
                memmove(&s_session_cache_entries[i], &s_session_cache_entries[i + 1],
                        (s_session_cache_count - i - 1) * sizeof(session_cache_entry_t));
                s_session_cache_count--;
                break;
            }
        }

        ESP_LOGI(TAG, "Session %s cleared", chat_id);
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

void session_list(void)
{
    DIR *dir = opendir(MIMI_FATFS_SESSION_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open session directory");
        return;
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "tg_") && strstr(entry->d_name, ".jsonl")) {
            ESP_LOGI(TAG, "  Session: %s", entry->d_name);
            count++;
        }
    }
    closedir(dir);

    if (count == 0) {
        ESP_LOGI(TAG, "  No sessions found");
    }
}

void session_get_stats(int *cached_sessions, int *total_messages)
{
    if (cached_sessions) *cached_sessions = s_session_cache_count;

    if (total_messages) {
        int total = 0;
        for (int i = 0; i < s_session_cache_count; i++) {
            total += s_session_cache_entries[i].metadata.total_messages;
        }
        *total_messages = total;
    }
}
