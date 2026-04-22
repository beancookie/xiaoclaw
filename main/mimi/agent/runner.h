#pragma once

#include "esp_err.h"
#include "cJSON.h"
#include <stddef.h>
#include <stdbool.h>

/* Forward declarations */
#include "bus/message_bus.h"
#include "llm/llm_proxy.h"

/**
 * Agent Run Specification - configuration for a single agent execution.
 */
typedef struct {
    const char *system_prompt;           /**< System prompt string */
    cJSON *initial_messages;             /**< Initial messages array (caller owns, cloned on call) */
    const char *tools_json;              /**< Pre-built JSON string of tools array, or NULL */
    int max_iterations;                  /**< Max tool-use iterations (default: MIMI_AGENT_MAX_TOOL_ITER) */
    int max_tool_result_chars;          /**< Max chars per tool result (default: 8*1024) */
    const char *error_message;           /**< Error message on LLM failure (default: "Sorry, I encountered an error.") */
    bool concurrent_tools;               /**< Enable concurrent tool execution (default: false) */
    void *workspace;                    /**< Workspace path for file tools (optional) */
    void (*checkpoint_callback)(void *session_key, const char *phase, cJSON *checkpoint);  /**< Called after each phase */
    void *checkpoint_session_key;        /**< Session key passed to checkpoint_callback */
    const mimi_msg_t *current_msg;      /**< Current message for context (channel, chat_id) */
} AgentRunSpec;

/**
 * Agent Run Result - outcome of a single agent execution.
 */
typedef struct {
    char *final_content;                 /**< Final assistant response (caller frees with free()) */
    cJSON *messages;                    /**< Complete message history including tool exchanges (caller frees with cJSON_Delete) */
    int tools_used_count;              /**< Number of tools called */
    char tools_used[32][32];            /**< Names of tools used */
    int usage_prompt_tokens;            /**< Approximate prompt tokens used */
    int usage_completion_tokens;        /**< Approximate completion tokens used */
    const char *stop_reason;            /**< "completed", "max_iterations", "error", "tool_error" */
    char *error;                        /**< Error message if stop_reason is "error" or "tool_error" (caller frees with free()) */
    /* Closed-loop learning support (Hermes Agent) */
    bool task_success;                  /**< true if task succeeded (evaluated by learning_hook_evaluate) */
    char tool_sequence_json[2048];      /**< JSON array of {tool, input} for the entire task */
    int tool_sequence_len;              /**< Number of tool calls in tool_sequence_json */
} AgentRunResult;

/**
 * Initialize the agent runner.
 * @return ESP_OK on success
 */
esp_err_t agent_runner_init(void);

/**
 * Run the agent with the given specification.
 *
 * This function executes a ReAct loop:
 * 1. Send messages to LLM with tools
 * 2. If no tool_use in response, return final_content
 * 3. If tool_use, execute tools and continue loop
 * 4. Respect max_iterations limit
 *
 * @param spec   Run specification (caller owns, not modified)
 * @param result  Output result (caller allocates, runner fills)
 * @return ESP_OK on success (check result->stop_reason for outcome)
 */
esp_err_t agent_runner_run(const AgentRunSpec *spec, AgentRunResult *result);

/**
 * Build assistant message content array from LLM response.
 * Used internally and by agent_loop for history formatting.
 *
 * @param resp  LLM response
 * @return cJSON array with text and tool_use blocks (caller frees with cJSON_Delete)
 */
cJSON *agent_runner_build_assistant_content(const llm_response_t *resp);

/**
 * Default error message when LLM call fails
 */
#define AGENT_DEFAULT_ERROR_MESSAGE "Sorry, I encountered an error calling the AI model."

/**
 * Default max iterations
 */
#define AGENT_DEFAULT_MAX_ITERATIONS 10

/**
 * Default max tool result characters
 */
#define AGENT_DEFAULT_MAX_TOOL_RESULT_CHARS (8 * 1024)
