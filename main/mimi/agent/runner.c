#include "runner.h"
#include "context_builder.h"
#include "llm/llm_proxy.h"
#include "tools/tool_registry.h"
#include "mimi_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "agent_runner";

/* Snip history when approaching context window limit */
#define CONTEXT_SNIP_SAFETY_BUFFER 1024

static const char *DEFAULT_ERROR_MSG = AGENT_DEFAULT_ERROR_MESSAGE;

/* ─── Helper: build user message with tool_results ──────────────────────── */

static cJSON *build_tool_result_content(const llm_response_t *resp, const mimi_msg_t *msg,
                                        char *tool_output, size_t tool_output_size)
{
    cJSON *content = cJSON_CreateArray();

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];

        /* Execute tool */
        tool_output[0] = '\0';
        esp_err_t err = tool_registry_execute(call->name, call->input, tool_output, tool_output_size);

        if (err != ESP_OK) {
            snprintf(tool_output, tool_output_size, "Error: tool '%s' not found or failed", call->name);
        }

        ESP_LOGI(TAG, "Tool %s result: %d bytes", call->name, (int)strlen(tool_output));

        /* Build tool_result block */
        cJSON *result_block = cJSON_CreateObject();
        cJSON_AddStringToObject(result_block, "type", "tool_result");
        cJSON_AddStringToObject(result_block, "tool_use_id", call->id);
        cJSON_AddStringToObject(result_block, "content", tool_output);
        cJSON_AddItemToArray(content, result_block);
    }

    return content;
}

/* ─── Helper: build assistant content array ─────────────────────────────── */

cJSON *agent_runner_build_assistant_content(const llm_response_t *resp)
{
    cJSON *content = cJSON_CreateArray();

    if (resp->text && resp->text_len > 0) {
        cJSON *text_block = cJSON_CreateObject();
        cJSON_AddStringToObject(text_block, "type", "text");
        cJSON_AddStringToObject(text_block, "text", resp->text);
        cJSON_AddItemToArray(content, text_block);
    }

    for (int i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        cJSON *tool_block = cJSON_CreateObject();
        cJSON_AddStringToObject(tool_block, "type", "tool_use");
        cJSON_AddStringToObject(tool_block, "id", call->id);
        cJSON_AddStringToObject(tool_block, "name", call->name);

        cJSON *input = cJSON_Parse(call->input);
        if (input) {
            cJSON_AddItemToObject(tool_block, "input", input);
        } else {
            cJSON_AddItemToObject(tool_block, "input", cJSON_CreateObject());
        }

        cJSON_AddItemToArray(content, tool_block);
    }

    return content;
}

/* ─── Helper: append assistant message to messages array ─────────────────── */

static void append_assistant_message(cJSON *messages, cJSON *content)
{
    cJSON *asst_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(asst_msg, "role", "assistant");
    cJSON_AddItemToObject(asst_msg, "content", content);
    cJSON_AddItemToArray(messages, asst_msg);
}

/* ─── Helper: append tool result message to messages array ───────────────── */

static void append_tool_result_message(cJSON *messages, cJSON *tool_results)
{
    cJSON *result_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(result_msg, "role", "user");
    cJSON_AddItemToObject(result_msg, "content", tool_results);
    cJSON_AddItemToArray(messages, result_msg);
}

/* ─── Helper: append final text as assistant message ────────────────────── */

static void append_final_message(cJSON *messages, const char *text)
{
    cJSON *content = cJSON_CreateArray();
    cJSON *text_block = cJSON_CreateObject();
    cJSON_AddStringToObject(text_block, "type", "text");
    cJSON_AddStringToObject(text_block, "text", text ? text : "");
    cJSON_AddItemToArray(content, text_block);

    cJSON *asst_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(asst_msg, "role", "assistant");
    cJSON_AddItemToObject(asst_msg, "content", content);
    cJSON_AddItemToArray(messages, asst_msg);
}

/* ─── Helper: estimate token count (rough heuristic) ────────────────────── */

static int estimate_token_count(const char *str)
{
    if (!str) return 0;
    /* Rough estimate: ~4 chars per token for Chinese/English mix */
    return strlen(str) / 4;
}

static int estimate_cjson_tokens(cJSON *obj)
{
    if (!obj) return 0;

    char *printed = cJSON_PrintUnformatted(obj);
    if (!printed) return 0;

    int tokens = estimate_token_count(printed);
    free(printed);
    return tokens;
}

/* ─── Helper: snip history to fit context window ─────────────────────────── */

static void snip_history(cJSON *messages, int max_context_tokens)
{
    if (!messages || !cJSON_IsArray(messages)) return;

    int current_tokens = 0;
    int msg_count = cJSON_GetArraySize(messages);
    int safety_budget = CONTEXT_SNIP_SAFETY_BUFFER / 4;
    int target_tokens = max_context_tokens - safety_budget;

    /* Count tokens from oldest to newest */
    int *msg_tokens = malloc(sizeof(int) * msg_count);
    if (!msg_tokens) return;

    for (int i = 0; i < msg_count; i++) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        msg_tokens[i] = estimate_cjson_tokens(msg);
        current_tokens += msg_tokens[i];
    }

    if (current_tokens <= target_tokens) {
        free(msg_tokens);
        return;
    }

    /* Remove oldest messages until we fit */
    int remove_count = 0;
    for (int i = 0; i < msg_count - 2; i++) {  /* Keep at least 2 messages */
        if (current_tokens - msg_tokens[i] <= target_tokens) {
            break;
        }
        current_tokens -= msg_tokens[i];
        remove_count++;
    }

    if (remove_count > 0) {
        ESP_LOGI(TAG, "Snipping %d oldest messages (%d -> %d tokens)", remove_count, current_tokens + msg_tokens[remove_count - 1], current_tokens);
        for (int i = 0; i < remove_count; i++) {
            cJSON_DeleteItemFromArray(messages, 0);
        }
    }

    free(msg_tokens);
}

/* ─── Main runner ───────────────────────────────────────────────────────── */

esp_err_t agent_runner_init(void)
{
    ESP_LOGI(TAG, "Agent runner initialized");
    return ESP_OK;
}

esp_err_t agent_runner_run(const AgentRunSpec *spec, AgentRunResult *result)
{
    if (!spec || !result) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    result->stop_reason = "completed";

    int max_iter = spec->max_iterations > 0 ? spec->max_iterations : AGENT_DEFAULT_MAX_ITERATIONS;
    int max_tool_chars = spec->max_tool_result_chars > 0 ? spec->max_tool_result_chars : AGENT_DEFAULT_MAX_TOOL_RESULT_CHARS;
    const char *error_msg = spec->error_message ? spec->error_message : DEFAULT_ERROR_MSG;

    /* Clone initial_messages since we modify it */
    cJSON *messages = cJSON_Duplicate(spec->initial_messages, true);
    if (!messages) {
        messages = cJSON_CreateArray();
    }

    char *tool_output = heap_caps_calloc(1, max_tool_chars, MALLOC_CAP_SPIRAM);
    if (!tool_output) {
        ESP_LOGE(TAG, "Failed to allocate tool output buffer");
        cJSON_Delete(messages);
        return ESP_ERR_NO_MEM;
    }

    char *final_text = NULL;
    const char *stop_reason = "completed";
    int iteration = 0;
    int tools_used_idx = 0;

    while (iteration < max_iter) {
        /* Check context window and snip if needed */
        int context_tokens = estimate_cjson_tokens(messages) + estimate_token_count(spec->system_prompt);
        int context_limit = MIMI_CONTEXT_BUF_SIZE / 4;  /* Rough token estimate */
        if (context_tokens > context_limit) {
            snip_history(messages, context_limit);
        }

        llm_response_t resp;
        esp_err_t err = llm_chat_tools(spec->system_prompt, messages, spec->tools_json, &resp);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LLM call failed: %s", esp_err_to_name(err));
            stop_reason = "error";
            result->error = strdup(error_msg);
            final_text = strdup(error_msg);
            llm_response_free(&resp);
            break;
        }

        if (!resp.tool_use) {
            /* No tool use - normal completion */
            if (resp.text && resp.text_len > 0) {
                final_text = strdup(resp.text);
                append_final_message(messages, final_text);
            }
            stop_reason = "completed";
            llm_response_free(&resp);
            break;
        }

        ESP_LOGI(TAG, "Iteration %d: tool_use with %d calls", iteration + 1, resp.call_count);

        /* Append assistant message with content array */
        cJSON *asst_content = agent_runner_build_assistant_content(&resp);
        append_assistant_message(messages, asst_content);

        /* Track tools used */
        for (int i = 0; i < resp.call_count && tools_used_idx < 32; i++) {
            strncpy(result->tools_used[tools_used_idx], resp.calls[i].name, 31);
            result->tools_used[tools_used_idx][31] = '\0';
            tools_used_idx++;
        }
        result->tools_used_count = tools_used_idx;

        /* Emit checkpoint before tool execution */
        if (spec->checkpoint_callback) {
            cJSON *checkpoint = cJSON_CreateObject();
            cJSON_AddStringToObject(checkpoint, "phase", "awaiting_tools");
            cJSON_AddNumberToObject(checkpoint, "iteration", iteration);
            cJSON_AddItemToObject(checkpoint, "assistant_message", cJSON_Duplicate(asst_content, true));
            spec->checkpoint_callback(spec->checkpoint_session_key, "awaiting_tools", checkpoint);
            cJSON_Delete(checkpoint);
        }

        /* Execute tools and append results */
        cJSON *tool_results = build_tool_result_content(&resp, spec->current_msg, tool_output, max_tool_chars);
        append_tool_result_message(messages, tool_results);

        llm_response_free(&resp);
        iteration++;
    }

    if (iteration >= max_iter) {
        stop_reason = "max_iterations";
        if (!final_text) {
            final_text = strdup("Task completed with maximum iterations reached.");
        }
        append_final_message(messages, final_text);
    }

    /* Build result */
    result->final_content = final_text;
    result->messages = messages;
    result->stop_reason = stop_reason;
    result->usage_prompt_tokens = estimate_cjson_tokens(spec->initial_messages);
    result->usage_completion_tokens = final_text ? estimate_token_count(final_text) : 0;

    free(tool_output);

    ESP_LOGI(TAG, "Agent run complete: stop_reason=%s, iterations=%d, tools=%d",
             stop_reason, iteration, result->tools_used_count);

    return ESP_OK;
}
