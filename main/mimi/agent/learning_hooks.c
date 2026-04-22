#include "agent/learning_hooks.h"
#include "agent/runner.h"
#include "skills/skill_meta.h"
#include "skills/skill_crystallize.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "learning";

/* ─── Failure keywords in output ────────────────────────────────────── */

static const char *failure_keywords[] = {
    "error", "failed", "sorry", "cannot", "unable", "don't know",
    "couldn't", "wouldn't", "invalid", "not found", "exception",
    NULL
};

static bool contains_failure_keywords(const char *output)
{
    if (!output) return false;

    for (int i = 0; failure_keywords[i]; i++) {
        if (strstr(output, failure_keywords[i])) {
            return true;
        }
    }
    return false;
}

/* ─── Public API ──────────────────────────────────────────────────────── */

bool learning_hook_evaluate(const char *final_output, const char *tool_sequence_json)
{
    /* Check final_output is non-empty */
    if (!final_output || !final_output[0]) {
        ESP_LOGI(TAG, "Evaluate: empty final_output -> false");
        return false;
    }

    /* Check for failure keywords */
    if (contains_failure_keywords(final_output)) {
        ESP_LOGI(TAG, "Evaluate: failure keywords found -> false");
        return false;
    }

    /* Check tool_sequence_json is non-empty */
    if (!tool_sequence_json || !tool_sequence_json[0]) {
        ESP_LOGI(TAG, "Evaluate: no tool sequence -> false");
        return false;
    }

    /* Must have at least one tool call in the JSON */
    cJSON *arr = cJSON_Parse(tool_sequence_json);
    if (!arr || !cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) {
        ESP_LOGI(TAG, "Evaluate: invalid or empty tool sequence JSON -> false");
        if (arr) cJSON_Delete(arr);
        return false;
    }
    cJSON_Delete(arr);

    ESP_LOGI(TAG, "Evaluate: task succeeded");
    return true;
}

void learning_hook_on_task_end(const char *chat_id, const void *result)
{
    if (!result) {
        return;
    }

    const AgentRunResult *r = (const AgentRunResult *)result;

    ESP_LOGI(TAG, "Task end: chat_id=%s, success=%d, tools=%d, tool_seq_len=%d",
             chat_id ? chat_id : "?", r->task_success, r->tools_used_count, r->tool_sequence_len);

    /* Record usage for each tool used */
    for (int i = 0; i < r->tools_used_count && i < 32; i++) {
        if (r->tools_used[i][0]) {
            skill_meta_record_usage(r->tools_used[i], r->task_success);
        }
    }

    /* Check for skill crystallization */
    if (r->task_success && r->tool_sequence_len > 1) {
        /* Build crystallization context */
        crystallize_context_t ctx = {
            .last_task_success = true,
            .step_count = r->tool_sequence_len,
            .is_repetitive = false,  /* TODO: detect repetitive tasks */
            .user_intent = NULL,      /* TODO: extract from messages */
            .tool_sequence_json = r->tool_sequence_json,
            .sequence_len = r->tool_sequence_len,
        };

        esp_err_t err = skill_crystallize_if_needed(&ctx);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Skill crystallized successfully");
        }
    }
}

/* ─── Default hooks structure ────────────────────────────────────────── */

const AgentHooks learning_hooks_default = {
    .before_iteration = NULL,
    .after_iteration = NULL,
    .before_tool_execute = NULL,
    .after_tool_execute = NULL,
    .evaluate_task_result = learning_hook_evaluate,
    .on_task_end = learning_hook_on_task_end,
};