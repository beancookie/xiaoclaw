#include "consolidator.h"
#include "session_manager.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "consolidator";

/* Archive subdirectory within session dir */
#define ARCHIVE_DIR MIMI_SPIFFS_SESSION_DIR "/archive"
#define ARCHIVE_FILE_PREFIX ARCHIVE_DIR "/tg_"

/* Configuration */
static consolidator_config_t s_config;

/* Statistics */
static int s_total_checks = 0;
static int s_sessions_consolidated = 0;

/* ─── Helper: ensure archive directory exists ───────────────────────── */

static esp_err_t ensure_archive_dir(void)
{
    /* SPIFFS is flat, but we use a prefix for archive files */
    /* Just verify the parent directory is accessible */
    return ESP_OK;
}

/* ─── Helper: get archive file path ─────────────────────────────────── */

static void archive_path(const char *chat_id, char *buf, size_t size)
{
    snprintf(buf, size, "%s%s.archive", ARCHIVE_FILE_PREFIX, chat_id);
}

/* ─── Helper: simple consolidation (truncate oldest) ────────────────── */

static esp_err_t consolidate_session(const char *chat_id)
{
    session_metadata_t meta;
    esp_err_t err = session_get_metadata(chat_id, &meta);
    if (err != ESP_OK) return err;

    /* Nothing to consolidate */
    if (meta.total_messages <= s_config.max_history) {
        return ESP_OK;
    }

    /* Calculate how many to archive */
    int to_archive = meta.total_messages - s_config.max_history;
    if (to_archive > s_config.consolidate_batch) {
        to_archive = s_config.consolidate_batch;
    }

    ESP_LOGI(TAG, "Consolidating %d messages for session %s (total=%d, threshold=%d)",
             to_archive, chat_id, meta.total_messages, s_config.max_history);

    /* Read oldest messages to archive */
    char path[64];
    session_get_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return ESP_FAIL;

    /* Collect first N lines for archive */
    char *archive_lines[256];
    int archive_count = 0;

    char line[4096];
    while (fgets(line, sizeof(line), f) && archive_count < to_archive && archive_count < 256) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;
        archive_lines[archive_count++] = strdup(line);
    }
    fclose(f);

    /* Write to archive file */
    char archive_path_buf[96];
    archive_path(chat_id, archive_path_buf, sizeof(archive_path_buf));

    FILE *af = fopen(archive_path_buf, "a");
    if (af) {
        for (int i = 0; i < archive_count; i++) {
            fprintf(af, "%s\n", archive_lines[i]);
            free(archive_lines[i]);
        }
        fclose(af);
    } else {
        /* Failed to open archive, just discard */
        for (int i = 0; i < archive_count; i++) {
            free(archive_lines[i]);
        }
        ESP_LOGW(TAG, "Cannot open archive file, messages will be discarded");
    }

    /* Create new session file without archived messages */
    FILE *tmp = fopen(path, "r");
    if (!tmp) return ESP_ERR_NOT_FOUND;

    /* Write to temp file */
    char tmp_path[72];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *out = fopen(tmp_path, "w");
    if (!out) {
        fclose(tmp);
        return ESP_FAIL;
    }

    /* Skip first archive_count lines */
    char linebuf[4096];
    int skipped = 0;
    while (fgets(linebuf, sizeof(linebuf), tmp)) {
        if (skipped < archive_count) {
            skipped++;
            continue;
        }
        fputs(linebuf, out);
    }

    fclose(tmp);
    fclose(out);

    /* Replace original */
    remove(path);
    rename(tmp_path, path);

    /* Update metadata */
    meta.total_messages -= archive_count;
    meta.last_consolidated = meta.total_messages;  /* Reset since we archived */
    meta.updated_at = time(NULL);

    /* Save metadata */
    err = session_mark_consolidated(chat_id, 0);  /* Reset consolidated count */

    /* Re-save full metadata */
    char meta_path[64];
    metadata_get_path(chat_id, meta_path, sizeof(meta_path));
    FILE *mf = fopen(meta_path, "w");
    if (mf) {
        fprintf(mf, "%d:%d:%d:%ld:%ld\n",
                0, meta.total_messages, meta.cursor,
                (long)meta.created_at, (long)meta.updated_at);
        fclose(mf);
    }

    s_sessions_consolidated++;

    ESP_LOGI(TAG, "Consolidation complete for %s: archived %d, remaining %d",
             chat_id, archive_count, meta.total_messages);

    return ESP_OK;
}

/* ─── Public API ─────────────────────────────────────────────────────── */

esp_err_t consolidator_init(consolidator_config_t *config)
{
    if (config) {
        s_config = *config;
    } else {
        s_config.max_history = 50;
        s_config.consolidate_batch = 20;
        s_config.archive_max_lines = 500;
    }

    s_total_checks = 0;
    s_sessions_consolidated = 0;

    ensure_archive_dir();

    ESP_LOGI(TAG, "Consolidator initialized (max_history=%d, batch=%d)",
             s_config.max_history, s_config.consolidate_batch);
    return ESP_OK;
}

esp_err_t consolidator_check_and_run(const char *chat_id)
{
    s_total_checks++;

    session_metadata_t meta;
    esp_err_t err = session_get_metadata(chat_id, &meta);
    if (err != ESP_OK) return err;

    /* Check if consolidation needed */
    if (meta.total_messages <= s_config.max_history) {
        return ESP_OK;
    }

    /* Run consolidation */
    return consolidate_session(chat_id);
}

esp_err_t consolidator_force_run(const char *chat_id)
{
    ESP_LOGW(TAG, "Force consolidation for %s", chat_id);
    return consolidate_session(chat_id);
}

void consolidator_get_stats(int *total_sessions, int *consolidated)
{
    if (total_sessions) *total_sessions = s_total_checks;
    if (consolidated) *consolidated = s_sessions_consolidated;
}
