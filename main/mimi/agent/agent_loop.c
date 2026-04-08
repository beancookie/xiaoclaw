#include "agent_loop.h"
#include "agent/context_builder.h"
#include "agent/runner.h"
#include "agent/checkpoint.h"
#include "mimi_config.h"
#include "bus/message_bus.h"
#include "memory/session_manager.h"
#include "tools/tool_registry.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "agent";

/* Forward declaration */
static void agent_loop_task(void *arg);

esp_err_t agent_loop_init(void)
{
    /* Initialize runner subsystem */
    agent_runner_init();

    ESP_LOGI(TAG, "Agent loop initialized");
    return ESP_OK;
}

esp_err_t agent_loop_start(void)
{
    const uint32_t stack_candidates[] = {
        MIMI_AGENT_STACK,
        20 * 1024,
        16 * 1024,
        14 * 1024,
        12 * 1024,
    };

    for (size_t i = 0; i < (sizeof(stack_candidates) / sizeof(stack_candidates[0])); i++) {
        uint32_t stack_size = stack_candidates[i];
        BaseType_t ret = xTaskCreatePinnedToCore(
            agent_loop_task, "agent_loop",
            stack_size, NULL,
            MIMI_AGENT_PRIO, NULL, MIMI_AGENT_CORE);

        if (ret == pdPASS) {
            ESP_LOGI(TAG, "agent_loop task created with stack=%u bytes", (unsigned)stack_size);
            return ESP_OK;
        }

        ESP_LOGW(TAG,
                 "agent_loop create failed (stack=%u, free_internal=%u, largest_internal=%u), retrying...",
                 (unsigned)stack_size,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }

    return ESP_FAIL;
}

/* ─── Main Agent Loop Task ────────────────────────────────────────────── */

static void agent_loop_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Agent loop started on core %d", xPortGetCoreID());

    /* Allocate buffers from PSRAM */
    char *system_prompt = heap_caps_calloc(1, MIMI_CONTEXT_BUF_SIZE, MALLOC_CAP_SPIRAM);
    char *history_json = heap_caps_calloc(1, MIMI_LLM_STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);

    if (!system_prompt || !history_json) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffers");
        vTaskDelete(NULL);
        return;
    }

    const char *tools_json = tool_registry_get_tools_json();

    while (1) {
        mimi_msg_t msg;
        esp_err_t err = message_bus_pop_inbound(&msg, portMAX_DELAY);
        if (err != ESP_OK) continue;

        ESP_LOGI(TAG, "Processing message from %s:%s", msg.channel, msg.chat_id);

        /* Skip LLM processing for non-xiaozhi channels */
        if (strcmp(msg.channel, MIMI_CHAN_XIAOZHI) != 0) {
            ESP_LOGI(TAG, "Skipping LLM for channel: %s", msg.channel);
            free(msg.content);
            continue;
        }

        /* Check for pending checkpoint to restore */
        checkpoint_phase_t phase;
        int checkpoint_iter;
        cJSON *checkpoint_data = NULL;

        if (checkpoint_exists(msg.chat_id)) {
            ESP_LOGI(TAG, "Found pending checkpoint for %s, attempting restore", msg.chat_id);
            if (checkpoint_load(msg.chat_id, &phase, &checkpoint_iter, &checkpoint_data) == ESP_OK) {
                ESP_LOGI(TAG, "Restored checkpoint: phase=%d iter=%d", phase, checkpoint_iter);
                /* TODO: Implement full checkpoint restoration in runner */
                checkpoint_clear(msg.chat_id);
            }
            if (checkpoint_data) {
                cJSON_Delete(checkpoint_data);
                checkpoint_data = NULL;
            }
        }

        /* Send "working" indicator */
#if MIMI_AGENT_SEND_WORKING_STATUS
        mimi_msg_t status = {0};
        strncpy(status.channel, msg.channel, sizeof(status.channel) - 1);
        strncpy(status.chat_id, msg.chat_id, sizeof(status.chat_id) - 1);
        status.content = strdup("思考中...");
        if (status.content) {
            if (message_bus_push_outbound(&status) != ESP_OK) {
                ESP_LOGW(TAG, "Outbound queue full, drop working status");
                free(status.content);
            }
        }
#endif

        /* 1. Build system prompt */
        context_build_system_prompt(system_prompt, MIMI_CONTEXT_BUF_SIZE);
        ESP_LOGI(TAG, "LLM turn context: channel=%s chat_id=%s", msg.channel, msg.chat_id);

        /* 2. Load session history */
        session_get_history_json(msg.chat_id, history_json,
                                MIMI_LLM_STREAM_BUF_SIZE, MIMI_AGENT_MAX_HISTORY);

        cJSON *messages = cJSON_Parse(history_json);
        if (!messages) {
            messages = cJSON_CreateArray();
        }

        /* 3. Append current user message with runtime context */
        {
            char runtime_ctx[512];
            context_build_runtime_context(runtime_ctx, sizeof(runtime_ctx), msg.channel, msg.chat_id);

            cJSON *user_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(user_msg, "role", "user");
            cJSON_AddStringToObject(user_msg, "content",
                snprintf(NULL, 0, "%s\n\n%s", runtime_ctx, msg.content ? msg.content : "") > 0
                    ? (char[]){"\0"} : (char[]){"\0"});  /* placeholder */
            /* Actually build proper content */
            char combined[1024];
            snprintf(combined, sizeof(combined), "%s\n\n%s", runtime_ctx, msg.content ? msg.content : "");
            cJSON_ReplaceItemInObject(user_msg, "content", cJSON_CreateString(combined));
            cJSON_AddItemToArray(messages, user_msg);
        }

        /* 4. Build run spec and execute via runner */
        AgentRunSpec spec = {
            .system_prompt = system_prompt,
            .initial_messages = messages,
            .tools_json = tools_json,
            .max_iterations = MIMI_AGENT_MAX_TOOL_ITER,
            .max_tool_result_chars = 8 * 1024,
            .error_message = "Sorry, I encountered an error.",
            .concurrent_tools = false,
            .current_msg = &msg,
        };

        AgentRunResult result;
        err = agent_runner_run(&spec, &result);

        /* 5. Handle result */
        if (err != ESP_OK || !result.final_content || !result.final_content[0]) {
            /* Error response */
            mimi_msg_t out = {0};
            strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
            strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
            out.content = strdup("Sorry, I encountered an error.");
            if (out.content && message_bus_push_outbound(&out) != ESP_OK) {
                free(out.content);
            }
        } else {
            /* Save to session */
            session_append(msg.chat_id, "user", msg.content);
            session_append(msg.chat_id, "assistant", result.final_content);

            /* Push response to outbound */
            mimi_msg_t out = {0};
            strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
            strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
            out.content = result.final_content;  /* transfer ownership */
            ESP_LOGI(TAG, "Queue final response to %s:%s (%d bytes)",
                     out.channel, out.chat_id, (int)strlen(out.content));
            if (message_bus_push_outbound(&out) != ESP_OK) {
                ESP_LOGW(TAG, "Outbound queue full, drop final response");
                free(out.content);
            }
            result.final_content = NULL;  /* prevent double-free */
        }

        /* Clear checkpoint on successful completion */
        checkpoint_clear(msg.chat_id);

        /* Cleanup */
        cJSON_Delete(messages);
        if (result.messages) cJSON_Delete(result.messages);
        if (result.final_content) free(result.final_content);
        if (result.error) free(result.error);
        free(msg.content);

        /* Log memory status */
        ESP_LOGI(TAG, "Free PSRAM: %d bytes",
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}
