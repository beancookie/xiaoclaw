#pragma once

#include "cJSON.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * Agent Hooks - simplified callback interface for agent events.
 *
 * All callbacks are optional - set to NULL if not needed.
 */
typedef struct {
    /** Called before each LLM iteration */
    void (*before_iteration)(int iteration);

    /** Called after each LLM iteration */
    void (*after_iteration)(int iteration, const char *final_content);

    /** Called before each tool execution */
    void (*before_tool_execute)(const char *tool_name);

    /** Called after each tool execution */
    void (*after_tool_execute)(const char *tool_name, const char *result, bool success);

    /* Closed-loop learning hooks (Hermes Agent) */
    /** Evaluate task success based on final output and tool sequence */
    bool (*evaluate_task_result)(const char *final_output, const char *tool_sequence_json);

    /** Called when a task ends - update metadata, check crystallization */
    void (*on_task_end)(const char *chat_id, const void *result);
} AgentHooks;

/**
 * Default empty hooks (all NULL).
 */
extern const AgentHooks AGENT_HOOKS_NONE;

/* ─── Inline helper macros ─────────────────────────────────────────────── */

#define HOOK_CALL(hooks, name, ...) \
    do { if ((hooks) && (hooks)->name) (hooks)->name(__VA_ARGS__); } while(0)

#define HOOK_BEFORE_ITERATION(hooks, iter) \
    HOOK_CALL(hooks, before_iteration, iter)

#define HOOK_AFTER_ITERATION(hooks, iter, content) \
    HOOK_CALL(hooks, after_iteration, iter, content)

#define HOOK_BEFORE_TOOL(hooks, name) \
    HOOK_CALL(hooks, before_tool_execute, name)

#define HOOK_AFTER_TOOL(hooks, name, result, ok) \
    HOOK_CALL(hooks, after_tool_execute, name, result, ok)
