#pragma once

#include "agent/hook.h"
#include <stdbool.h>

/**
 * Learning Hooks - default implementations for closed-loop learning.
 *
 * These hooks provide:
 * - Task success evaluation (learning_hook_evaluate)
 * - Task end processing (learning_hook_on_task_end)
 *
 * Used to implement the Hermes Agent pattern of闭环学习 (closed-loop learning).
 */

/**
 * Evaluate whether a task succeeded based on final output and tool sequence.
 *
 * Success criteria:
 * - final_output is non-empty
 * - final_output doesn't contain failure keywords (error, failed, sorry, cannot, unable)
 * - tool_sequence_json is non-empty (at least one tool was called)
 *
 * @param final_output       The final assistant response
 * @param tool_sequence_json JSON array of tool calls made during the task
 * @return true if task succeeded
 */
bool learning_hook_evaluate(const char *final_output, const char *tool_sequence_json);

/**
 * Called when a task ends. Updates skill metadata and checks for crystallization.
 *
 * Actions:
 * 1. Record usage for each tool used in the task
 * 2. If task succeeded and step_count > 1, trigger skill crystallization check
 *
 * @param chat_id  Chat identifier for this conversation
 * @param result   AgentRunResult with task outcome
 */
void learning_hook_on_task_end(const char *chat_id, const void *result);

/**
 * Default learning hooks structure (all callbacks set).
 */
extern const AgentHooks learning_hooks_default;