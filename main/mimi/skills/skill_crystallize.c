#include "skills/skill_crystallize.h"
#include "skills/skill_meta.h"
#include "mimi_config.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "crystallize";

/* ─── Helper: simple hash for intent (UTF-8 aware) ─────────────────── */

static void simple_hash(const char *str, char *out, size_t out_size)
{
    unsigned int hash = 5381;
    for (const char *p = str; *p; p++) {
        /* Skip UTF-8 continuation bytes (0x80-0xBF) */
        if ((unsigned char)*p >= 0x80) {
            continue;
        }
        hash = ((hash << 5) + hash) + (unsigned char)*p;
    }
    snprintf(out, out_size, "%04x", hash & 0xffff);
}

/* ─── Helper: extract first 3 significant ASCII words from intent ─── */

static void extract_intent_prefix(const char *intent, char *out, size_t out_size)
{
    if (!intent) {
        out[0] = '\0';
        return;
    }

    size_t i = 0;
    int word_count = 0;
    int char_count = 0;

    while (intent[i] && word_count < 3 && char_count < out_size - 1) {
        unsigned char c = (unsigned char)intent[i];

        /* Skip UTF-8 multi-byte characters (Chinese, etc.) */
        if (c >= 0x80) {
            i++;
            continue;
        }

        if (isspace(c)) {
            if (char_count > 0) {
                out[char_count++] = '_';
                word_count++;
            }
        } else if (isalnum(c)) {
            out[char_count++] = tolower(c);
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
    char dir[384];
    snprintf(dir, sizeof(dir), "%s", path);

    /* Create each directory component */
    for (char *p = dir; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (strlen(dir) > 0) {
                mkdir(dir, 0755);
            }
            *p = '/';
        }
    }
    /* Create final directory */
    if (strlen(dir) > 0) {
        mkdir(dir, 0755);
    }
    return ESP_OK;
}

/* ─── Public API ──────────────────────────────────────────────────────── */

esp_err_t skill_crystallize_init(void)
{
    ESP_LOGI(TAG, "Skill crystallization subsystem initialized");
    return ESP_OK;
}

/* ─── Helper: calculate quality score from crystallization context ─── */

static int calc_quality_score(const crystallize_context_t *ctx)
{
    if (!ctx) return 0;

    /* Clarity: workflow complexity — more steps = clearer procedure */
    int clarity = 50;
    if (ctx->step_count >= 2) clarity += 10;
    if (ctx->step_count >= 3) clarity += 10;
    if (ctx->step_count >= 5) clarity += 10;
    if (clarity > 100) clarity = 100;

    /* Completeness: proven reliability — success + repetition */
    int completeness = 50;
    if (ctx->last_task_success) completeness += 20;
    if (ctx->is_repetitive) completeness += 20;
    if (ctx->step_count >= 3) completeness += 10;
    if (completeness > 100) completeness = 100;

    /* Actionability: can be reproduced — success + sufficient steps */
    int actionability = 50;
    if (ctx->last_task_success) actionability += 25;
    if (ctx->step_count >= 2) actionability += 15;
    if (ctx->step_count >= 4) actionability += 10;
    if (actionability > 100) actionability = 100;

    /* Weighted overall score */
    return (int)(clarity * 0.3 + completeness * 0.3 + actionability * 0.4);
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
    if (ctx->step_count < 2) {
        ESP_LOGI(TAG, "Crystallize skipped: step_count=%d (need >= 2)", ctx->step_count);
        return false;
    }

    /* Check quality threshold */
    int quality_score = calc_quality_score(ctx);
    if (quality_score < SKILL_QUALITY_THRESHOLD_MIN) {
        ESP_LOGI(TAG, "Crystallize skipped: quality_score=%d (threshold=%d)",
                 quality_score, SKILL_QUALITY_THRESHOLD_MIN);
        return false;
    }

    /* Must not have similar skill - use LLM for semantic matching */
    if (ctx->user_intent) {
        char matched_skill[64];
        if (skill_meta_similar_exists_jaccard(ctx->user_intent, matched_skill, sizeof(matched_skill))) {
            ESP_LOGI(TAG, "Crystallize skipped: similar skill '%s' exists", matched_skill);
            return false;
        }
    }

    ESP_LOGI(TAG, "Crystallize conditions met: success=true, steps=%d, quality=%d",
             ctx->step_count, quality_score);
    return true;
}

void skill_crystallize_generate_name(const char *intent, char *buf, size_t size)
{
    char prefix[32];
    extract_intent_prefix(intent ? intent : "task", prefix, sizeof(prefix));

    /* Fallback to "task" if prefix is empty */
    if (prefix[0] == '\0') {
        snprintf(prefix, sizeof(prefix), "task");
    }

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
    snprintf(skill_dir, sizeof(skill_dir), "%s/skills/auto/%s", MIMI_FATFS_BASE, name);

    ensure_dir(skill_dir);

    /* Create SKILL.md file */
    char skill_path[320];
    snprintf(skill_path, sizeof(skill_path), "%s/SKILL.md", skill_dir);

    FILE *f = fopen(skill_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create %s", skill_path);
        return ESP_FAIL;
    }

    /* Calculate success rate and quality scores */
    float success_rate = 1.0f;
    int clarity = 50, completeness = 50, actionability = 50;
    if (seq_len >= 2) clarity += 10;
    if (seq_len >= 3) clarity += 10;
    if (seq_len >= 5) clarity += 10;
    if (clarity > 100) clarity = 100;
    if (seq_len >= 2) completeness += 10;
    if (seq_len >= 3) completeness += 10;
    if (seq_len >= 5) completeness += 10;
    completeness += 10;  /* success */
    if (completeness > 100) completeness = 100;
    actionability = 80;  /* success + multi-step */
    if (actionability > 100) actionability = 100;

    /* Extract unique tools from tool sequence */
    char tools[SKILL_MAX_TOOLS][32];
    int tool_count = 0;

    /* Write YAML frontmatter */
    fprintf(f, "---\n");
    fprintf(f, "name: %s\n", name);
    fprintf(f, "description: Auto-generated skill for: %s\n", intent ? intent : "unknown");
    fprintf(f, "always: false\n");
    fprintf(f, "auto: true\n");
    fprintf(f, "created_from: %d tool calls\n", seq_len);
    fprintf(f, "step_count: %d\n", seq_len);
    fprintf(f, "success_rate: %.1f\n", success_rate);
    fprintf(f, "quality_score: %d\n", (int)(clarity * 0.3 + completeness * 0.3 + actionability * 0.4));
    fprintf(f, "clarity: %d\n", clarity);
    fprintf(f, "completeness: %d\n", completeness);
    fprintf(f, "actionability: %d\n", actionability);
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

                /* Extract unique tool names */
                if (tool_count < SKILL_MAX_TOOLS) {
                    bool found = false;
                    for (int t = 0; t < tool_count; t++) {
                        if (strcmp(tools[t], tool->valuestring) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        strncpy(tools[tool_count], tool->valuestring, sizeof(tools[tool_count]) - 1);
                        tools[tool_count][sizeof(tools[tool_count]) - 1] = '\0';
                        tool_count++;
                    }
                }
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

    ESP_LOGI(TAG, "Created auto skill: %s (quality=%d, tools=%d)",
             skill_path,
             (int)(clarity * 0.3 + completeness * 0.3 + actionability * 0.4),
             tool_count);

    /* Add to skill index with quality scores */
    skill_meta_t meta = {0};
    strncpy(meta.name, name, sizeof(meta.name) - 1);
    strncpy(meta.path, skill_path, sizeof(meta.path) - 1);
    meta.is_auto = true;
    meta.usage_count = 0;
    meta.success_count = 0;
    meta.success_rate = 0.0;
    meta.last_used = 0;
    meta.is_hot = false;

    /* Quality scores */
    meta.clarity = clarity;
    meta.completeness = completeness;
    meta.actionability = actionability;

    /* Extract tools to metadata */
    for (int i = 0; i < tool_count && i < SKILL_MAX_TOOLS; i++) {
        strncpy(meta.tools[i], tools[i], sizeof(meta.tools[i]) - 1);
        meta.tool_count++;
    }

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