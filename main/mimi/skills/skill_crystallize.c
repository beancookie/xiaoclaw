#include "skills/skill_crystallize.h"
#include "skills/skill_meta.h"
#include "mimi_config.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "crystallize";

/* ─── Helper: simple hash for intent ─────────────────────────────────── */

static void simple_hash(const char *str, char *out, size_t out_size)
{
    unsigned int hash = 5381;
    for (const char *p = str; *p; p++) {
        hash = ((hash << 5) + hash) + (unsigned char)*p;
    }
    snprintf(out, out_size, "%04x", hash & 0xffff);
}

/* ─── Helper: extract first 3 significant words from intent ─────────── */

static void extract_intent_prefix(const char *intent, char *out, size_t out_size)
{
    /* Copy first 3 words or ~20 chars, whichever is smaller */
    size_t i = 0;
    int word_count = 0;
    int char_count = 0;

    while (intent[i] && word_count < 3 && char_count < out_size - 1) {
        if (isspace((unsigned char)intent[i])) {
            if (char_count > 0) {
                out[char_count++] = '_';
                word_count++;
            }
        } else if (isalnum((unsigned char)intent[i])) {
            out[char_count++] = tolower((unsigned char)intent[i]);
        }
        i++;
    }

    /* Remove trailing underscore */
    if (char_count > 0 && out[char_count - 1] == '_') {
        char_count--;
    }
    out[char_count] = '\0';
}

/* ─── Helper: create parent directories ─────────────────────────────── */

static esp_err_t ensure_dir(const char *path)
{
    /* For SPIFFS, we just need to ensure the path is valid
       Directories are created implicitly when writing files */
    char dir[256];
    char *p = dir + snprintf(dir, sizeof(dir), "%s", path);

    while (p < dir + strlen(dir)) {
        if (*p == '/') {
            *p = '\0';
            FILE *f = fopen(dir, "w");
            if (f) fclose(f);
            *p = '/';
        }
        p++;
    }
    return ESP_OK;
}

/* ─── Public API ──────────────────────────────────────────────────────── */

esp_err_t skill_crystallize_init(void)
{
    ESP_LOGI(TAG, "Skill crystallization subsystem initialized");
    return ESP_OK;
}

bool skill_crystallize_should_create(const crystallize_context_t *ctx)
{
    if (!ctx) return false;

    /* Must succeed */
    if (!ctx->last_task_success) {
        ESP_LOGI(TAG, "Crystallize skipped: task failed");
        return false;
    }

    /* Must have more than one step */
    if (ctx->step_count <= 1) {
        ESP_LOGI(TAG, "Crystallize skipped: step_count=%d (need > 1)", ctx->step_count);
        return false;
    }

    /* For embedded devices with simple tasks, allow crystallization for step_count >= 2.
     * This enables learning from multi-step tasks even if not strictly "repetitive". */
    if (ctx->step_count < 2) {
        ESP_LOGI(TAG, "Crystallize skipped: step_count=%d (need >= 2)", ctx->step_count);
        return false;
    }

    /* Must not have similar skill */
    if (ctx->user_intent && skill_meta_similar_exists(ctx->user_intent)) {
        ESP_LOGI(TAG, "Crystallize skipped: similar skill exists");
        return false;
    }

    ESP_LOGI(TAG, "Crystallize conditions met: success=true, steps=%d", ctx->step_count);
    return true;
}

void skill_crystallize_generate_name(const char *intent, char *buf, size_t size)
{
    char prefix[32];
    extract_intent_prefix(intent ? intent : "task", prefix, sizeof(prefix));

    char hash_str[8];
    simple_hash(intent ? intent : "", hash_str, sizeof(hash_str));

    /* Add timestamp (last 4 hex digits of time) */
    time_t now = time(NULL);
    snprintf(buf, size, "auto_%s_%s%04x", prefix, hash_str, (unsigned int)(now & 0xffff));
}

esp_err_t skill_crystallize_create(const char *name, const char *intent,
                                     const char *tool_seq, int seq_len)
{
    if (!name || !tool_seq) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Create skill directory */
    char skill_dir[256];
    snprintf(skill_dir, sizeof(skill_dir), "%s/skills/auto/%s", MIMI_SPIFFS_BASE, name);

    ensure_dir(skill_dir);

    /* Create SKILL.md file */
    char skill_path[320];
    snprintf(skill_path, sizeof(skill_path), "%s/SKILL.md", skill_dir);

    FILE *f = fopen(skill_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create %s", skill_path);
        return ESP_FAIL;
    }

    /* Calculate success rate from tool sequence (assume success for now) */
    float success_rate = 1.0f;

    /* Write YAML frontmatter */
    fprintf(f, "---\n");
    fprintf(f, "name: %s\n", name);
    fprintf(f, "description: Auto-generated skill for: %s\n", intent ? intent : "unknown");
    fprintf(f, "always: false\n");
    fprintf(f, "auto: true\n");
    fprintf(f, "created_from: %d tool calls\n", seq_len);
    fprintf(f, "step_count: %d\n", seq_len);
    fprintf(f, "success_rate: %.1f\n", success_rate);
    fprintf(f, "---\n\n");

    /* Write skill content */
    fprintf(f, "# Auto Skill: %s\n\n", name);
    fprintf(f, "## Intent\n%s\n\n", intent ? intent : "Auto-generated from task execution");
    fprintf(f, "## Trigger\n");
    fprintf(f, "This skill is triggered when a similar multi-step task is requested.\n\n");
    fprintf(f, "## Tool Sequence\n");

    /* Parse tool_seq JSON and write each tool */
    cJSON *arr = cJSON_Parse(tool_seq);
    if (arr && cJSON_IsArray(arr)) {
        int count = cJSON_GetArraySize(arr);
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(arr, i);
            cJSON *tool = cJSON_GetObjectItem(item, "tool");
            cJSON *input = cJSON_GetObjectItem(item, "input");

            if (tool && tool->valuestring) {
                fprintf(f, "%d. %s", i + 1, tool->valuestring);
                if (input && input->valuestring) {
                    fprintf(f, "(%s)", input->valuestring);
                }
                fprintf(f, "\n");
            }
        }
        cJSON_Delete(arr);
    } else {
        /* Fallback: just write the raw JSON */
        fprintf(f, "%s\n", tool_seq);
    }

    fprintf(f, "\n## Pitfalls\n");
    fprintf(f, "- This is an auto-generated skill from multi-step task execution\n");
    fprintf(f, "- Review the tool sequence before using in production\n");

    fclose(f);

    ESP_LOGI(TAG, "Created auto skill: %s", skill_path);

    /* Add to skill index */
    skill_meta_t meta = {0};
    strncpy(meta.name, name, sizeof(meta.name) - 1);
    strncpy(meta.path, skill_path, sizeof(meta.path) - 1);
    meta.is_auto = true;
    meta.usage_count = 0;
    meta.success_count = 0;
    meta.success_rate = 0.0;
    meta.last_used = 0;
    meta.is_hot = false;

    esp_err_t err = skill_meta_add(&meta);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add skill to index: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

esp_err_t skill_crystallize_if_needed(const crystallize_context_t *ctx)
{
    if (!ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!skill_crystallize_should_create(ctx)) {
        return ESP_ERR_NOT_FOUND;
    }

    /* Generate skill name */
    char skill_name[64];
    skill_crystallize_generate_name(ctx->user_intent, skill_name, sizeof(skill_name));

    ESP_LOGI(TAG, "Creating auto skill: %s", skill_name);

    return skill_crystallize_create(skill_name, ctx->user_intent,
                                      ctx->tool_sequence_json, ctx->sequence_len);
}