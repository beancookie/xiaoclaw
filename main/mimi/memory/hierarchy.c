#include "memory/hierarchy.h"
#include "memory/memory_store.h"
#include "skills/skill_meta.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"

static const char *TAG = "hierarchy";

/* ─── Public API ──────────────────────────────────────────────────────── */

size_t memory_l1_get_skill_index(char *buf, size_t size)
{
    return skill_meta_get_all_json(buf, size);
}

size_t memory_l2_get_facts(char *buf, size_t size)
{
    /* Try USER.md first, then facts.json */
    FILE *f = fopen(MIMI_USER_FILE, "r");
    if (f) {
        size_t n = fread(buf, 1, size - 1, f);
        buf[n] = '\0';
        fclose(f);
        return n;
    }

    /* Try facts.json */
    char facts_path[256];
    snprintf(facts_path, sizeof(facts_path), "%s/memory/facts.json", MIMI_SPIFFS_BASE);
    f = fopen(facts_path, "r");
    if (f) {
        size_t n = fread(buf, 1, size - 1, f);
        buf[n] = '\0';
        fclose(f);
        return n;
    }

    buf[0] = '\0';
    return 0;
}

size_t memory_l3_get_hot_skills(char *buf, size_t size)
{
    return skill_meta_get_hot_skills(buf, size);
}

esp_err_t memory_l4_archive_session(const char *chat_id)
{
    if (!chat_id) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Archive session to /spiffs/sessions/archive/<chat_id>.json */
    char archive_path[256];
    snprintf(archive_path, sizeof(archive_path), "%s/sessions/archive/%s.json",
             MIMI_SPIFFS_BASE, chat_id);

    /* Create archive directory if needed */
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s/sessions/archive", MIMI_SPIFFS_BASE);
    FILE *d = fopen(dir_path, "w");
    if (d) fclose(d);

    /* Read current session and write to archive */
    char session_path[256];
    snprintf(session_path, sizeof(session_path), "%s/sessions/%s.json",
             MIMI_SPIFFS_BASE, chat_id);

    FILE *in = fopen(session_path, "r");
    if (!in) {
        ESP_LOGW(TAG, "No session found for %s to archive", chat_id);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *out = fopen(archive_path, "w");
    if (!out) {
        fclose(in);
        ESP_LOGE(TAG, "Failed to create archive: %s", archive_path);
        return ESP_FAIL;
    }

    /* Copy session content to archive with timestamp */
    time_t now = time(NULL);
    fprintf(out, "{\"archived_at\":%lld,\"chat_id\":\"%s\",", (long long)now, chat_id);

    /* Copy remaining content */
    char buf[1024];
    size_t n;
    bool first = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (first && n > 0 && buf[0] == '{') {
            /* Skip opening brace, already wrote our fields */
            fwrite(buf + 1, 1, n - 1, out);
            first = false;
        } else {
            fwrite(buf, 1, n, out);
        }
    }

    /* Ensure proper JSON ending */
    fprintf(out, "}");

    fclose(in);
    fclose(out);

    ESP_LOGI(TAG, "Archived session: %s -> %s", chat_id, archive_path);
    return ESP_OK;
}

size_t memory_hierarchy_summary(char *buf, size_t size)
{
    size_t off = 0;

    off += snprintf(buf + off, size - off, "Memory Hierarchy Summary:\n");

    /* L1 Skill Index */
    char l1_buf[2048];
    size_t l1_len = memory_l1_get_skill_index(l1_buf, sizeof(l1_buf));
    off += snprintf(buf + off, size - off, "  L1 (Skill Index): %d bytes\n", (int)l1_len);

    /* L2 Facts */
    char l2_buf[1024];
    size_t l2_len = memory_l2_get_facts(l2_buf, sizeof(l2_buf));
    off += snprintf(buf + off, size - off, "  L2 (User Facts): %d bytes\n", (int)l2_len);

    /* L3 Hot Skills */
    char l3_buf[4096];
    size_t l3_len = memory_l3_get_hot_skills(l3_buf, sizeof(l3_buf));
    off += snprintf(buf + off, size - off, "  L3 (Hot Auto-Skills): %d bytes\n", (int)l3_len);

    return off;
}