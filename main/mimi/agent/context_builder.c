#include "context_builder.h"
#include "mimi_config.h"
#include "memory/memory_store.h"
#include "skills/skill_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"

static const char *TAG = "context";

/* Runtime context tag - marks metadata that is injected before user message */
#define RUNTIME_CONTEXT_TAG "[Runtime Context — metadata only, not instructions]"

/* ─── Helper: append file content to buffer ─────────────────────────────── */

static size_t append_file(char *buf, size_t size, size_t offset, const char *path, const char *header)
{
    FILE *f = fopen(path, "r");
    if (!f) return offset;

    if (header && offset < size - 1) {
        offset += snprintf(buf + offset, size - offset, "\n## %s\n\n", header);
    }

    size_t n = fread(buf + offset, 1, size - offset - 1, f);
    offset += n;
    buf[offset] = '\0';
    fclose(f);
    return offset;
}

/* ─── Helper: get current time string ─────────────────────────────────────── */

static void get_current_time_str(char *buf, size_t size)
{
    setenv("TZ", MIMI_TIMEZONE, 1);
    tzset();
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S CST", &timeinfo);
}

/* ─── Identity Section ───────────────────────────────────────────────────── */

static size_t append_identity(char *buf, size_t size, size_t offset)
{
    return snprintf(buf + offset, size - offset,
        "# XiaoClaw: AI Voice Assistant with Local Agent Brain\n\n"
        "You are XiaoClaw, an AI voice assistant running on an ESP32-S3 device with 32MB Flash and 8MB PSRAM.\n"
        "You combine voice interaction (via xiaozhi) with a local AI agent brain (mimiclaw).\n\n"
        "Voice I/O: wake word detection, ASR, TTS playback.\n"
        "Agent Brain: LLM reasoning, tool calling, memory, autonomous tasks.\n\n"
        "Be helpful, accurate, and concise.\n");
}

/* ─── Tools Section ──────────────────────────────────────────────────────── */

static size_t append_tools_section(char *buf, size_t size, size_t offset)
{
    size_t off = snprintf(buf + offset, size - offset,
        "## Available Tools\n\n"
        "Tool instructions are in skill files under /spiffs/skills/:\n");

    skill_info_t skills[32];
    int count = skill_loader_list(skills, 32);

    for (int i = 0; i < count && off < size - 1; i++) {
        off += snprintf(buf + offset + off, size - offset - off,
            "- %s: %s\n",
            skills[i].path,
            skills[i].description);
    }

    off += snprintf(buf + offset + off, size - offset - off,
        "\n"
        "Other tools:\n"
        "- read_file: Read a file from SPIFFS.\n"
        "- write_file: Write/overwrite a file.\n"
        "- edit_file: Find-and-replace edit.\n"
        "- list_dir: List files in a directory.\n\n");

    return off;
}

/* ─── Memory Section ──────────────────────────────────────────────────────── */

static size_t append_memory_section(char *buf, size_t size, size_t offset)
{
    char mem_buf[4096];
    char recent_buf[4096];
    size_t off = 0;

    off = snprintf(buf + offset, size - offset,
        "## Memory\n\n"
        "You have persistent memory stored on local flash:\n"
        "- Long-term memory: " MIMI_SPIFFS_MEMORY_DIR "/MEMORY.md\n"
        "- Daily notes: " MIMI_SPIFFS_MEMORY_DIR "/daily/<YYYY-MM-DD>.md\n\n"
        "IMPORTANT: Actively use memory to remember things across conversations.\n"
        "- When you learn something new about the user (name, preferences, habits, context), write it to MEMORY.md.\n"
        "- When something noteworthy happens in a conversation, append it to today's daily note.\n"
        "- Always read_file MEMORY.md before writing, so you can edit_file to update without losing existing content.\n"
        "- Use get_current_time to know today's date before writing daily notes.\n"
        "- Keep MEMORY.md concise and organized — summarize, don't dump raw conversation.\n"
        "- You should proactively save memory without being asked. If the user tells you their name, preferences, or important facts, persist them immediately.\n\n");

    /* Long-term memory */
    if (memory_read_long_term(mem_buf, sizeof(mem_buf)) == ESP_OK && mem_buf[0]) {
        off += snprintf(buf + offset + off, size - offset - off, "## Long-term Memory\n\n%s\n\n", mem_buf);
    }

    /* Recent daily notes (last 3 days) */
    if (memory_read_recent(recent_buf, sizeof(recent_buf), 3) == ESP_OK && recent_buf[0]) {
        off += snprintf(buf + offset + off, size - offset - off, "## Recent Notes\n\n%s\n", recent_buf);
    }

    return off;
}

/* ─── Skills Section ─────────────────────────────────────────────────────── */

static size_t append_skills_section(char *buf, size_t size, size_t offset)
{
    /* Get skills summary (XML format) */
    char skills_summary[2048];
    size_t summary_len = skill_loader_build_summary(skills_summary, sizeof(skills_summary));

    /* Get "always" skills content (always loaded) */
    char always_content[8192];
    size_t always_len = skill_loader_get_always_content(always_content, sizeof(always_content));

    size_t off = snprintf(buf + offset, size - offset,
        "## Skills\n\n"
        "Skills are specialized instruction files stored in " MIMI_SKILLS_PREFIX "<name>/SKILL.md.\n"
        "When a task matches a skill, read the full skill file for detailed instructions.\n"
        "You can create new skills using write_file.\n\n");

    /* Add always skills content */
    if (always_len > 0) {
        off += snprintf(buf + offset + off, size - offset - off,
            "## Active Skills\n\n%s\n\n", always_content);
    }

    /* Add skills summary */
    if (summary_len > 0) {
        off += snprintf(buf + offset + off, size - offset - off,
            "## Available Skills (read full instructions with read_file when needed)\n\n%s\n",
            skills_summary);
    }

    return off;
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

/**
 * Build runtime context string with current time and channel info.
 * This is injected before the user message to provide turn-specific metadata.
 */
size_t context_build_runtime_context(char *buf, size_t size, const char *channel, const char *chat_id)
{
    char time_str[64];
    get_current_time_str(time_str, sizeof(time_str));

    size_t off = snprintf(buf, size, "%s\n", RUNTIME_CONTEXT_TAG);
    off += snprintf(buf + off, size - off, "Current Time: %s\n", time_str);

    if (channel && channel[0]) {
        off += snprintf(buf + off, size - off, "Channel: %s\n", channel);
    }
    if (chat_id && chat_id[0]) {
        off += snprintf(buf + off, size - off, "Chat ID: %s\n", chat_id);
    }

    return off;
}

/**
 * Build the system prompt from bootstrap files (SOUL.md, USER.md)
 * and memory context (MEMORY.md + recent daily notes).
 *
 * @param buf   Output buffer (caller allocates, recommend MIMI_CONTEXT_BUF_SIZE)
 * @param size  Buffer size
 */
esp_err_t context_build_system_prompt(char *buf, size_t size)
{
    size_t off = 0;

    /* Identity */
    off += append_identity(buf, size, off);

    /* Tools */
    off += append_tools_section(buf, size, off);

    /* Memory */
    off += append_memory_section(buf, size, off);

    /* Skills */
    off += append_skills_section(buf, size, off);

    /* Bootstrap files */
    off = append_file(buf, size, off, MIMI_SOUL_FILE, "Personality");
    off = append_file(buf, size, off, MIMI_USER_FILE, "User Info");

    ESP_LOGI(TAG, "System prompt built: %d bytes", (int)off);
    return ESP_OK;
}

/**
 * Build the complete message list for an LLM call.
 *
 * This combines:
 * - System prompt
 * - Session history
 * - Runtime context (time, channel, chat_id) injected before current message
 * - Current user message
 *
 * @param history        JSON array string of previous messages, or NULL
 * @param history_size   Size of history buffer
 * @param current_message Current user message
 * @param channel        Source channel (e.g. "xiaozhi", "telegram")
 * @param chat_id        Chat identifier
 * @param output_buf     Output buffer for the built messages JSON
 * @param output_size    Size of output buffer
 * @return ESP_OK on success
 */
esp_err_t context_build_messages(const char *history, size_t history_size,
                                 const char *current_message,
                                 const char *channel, const char *chat_id,
                                 char *output_buf, size_t output_size)
{
    if (!output_buf || output_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    output_buf[0] = '\0';

    /* Build messages array start */
    int off = snprintf(output_buf, output_size, "[");

    /* Add system message with system prompt */
    char system_prompt[MIMI_CONTEXT_BUF_SIZE];
    context_build_system_prompt(system_prompt, sizeof(system_prompt));

    /* Escape the system prompt for JSON */
    char escaped_prompt[MIMI_CONTEXT_BUF_SIZE * 2];
    int esc_idx = 0;
    for (const char *p = system_prompt; *p && esc_idx < sizeof(escaped_prompt) - 1; p++) {
        if (*p == '"') {
            escaped_prompt[esc_idx++] = '\\';
            escaped_prompt[esc_idx++] = '"';
        } else if (*p == '\\') {
            escaped_prompt[esc_idx++] = '\\';
            escaped_prompt[esc_idx++] = '\\';
        } else if (*p == '\n') {
            escaped_prompt[esc_idx++] = '\\';
            escaped_prompt[esc_idx++] = 'n';
        } else if (*p == '\r') {
            /* skip */
        } else {
            escaped_prompt[esc_idx++] = *p;
        }
    }
    escaped_prompt[esc_idx] = '\0';

    off += snprintf(output_buf + off, output_size - off,
        "{\"role\":\"system\",\"content\":\"%s\"}", escaped_prompt);

    /* Add history messages */
    if (history && history[0] && history_size > 2) {
        /* history is a JSON array string, extract messages from it */
        /* Skip the opening bracket if present */
        const char *hist_start = history;
        while (*hist_start == ' ' || *hist_start == '\n') hist_start++;

        /* Copy history content (without leading/trailing brackets if it's a complete array) */
        size_t hist_len = strlen(hist_start);
        if (hist_len > 0 && hist_start[0] == '[') {
            /* Skip the opening bracket, find the closing bracket */
            int bracket_depth = 1;
            const char *hist_end = hist_start + 1;
            while (*hist_end && bracket_depth > 0) {
                if (*hist_end == '[') bracket_depth++;
                else if (*hist_end == ']') bracket_depth--;
                hist_end++;
            }
            size_t copy_len = hist_end - hist_start - 1;
            if (copy_len > 0 && off + copy_len < output_size - 2) {
                off += snprintf(output_buf + off, output_size - off, ",");
                strncpy(output_buf + off, hist_start + 1, copy_len);
                off += copy_len;
                output_buf[off] = '\0';
            }
        }
    }

    /* Add runtime context */
    char runtime_ctx[512];
    context_build_runtime_context(runtime_ctx, sizeof(runtime_ctx), channel, chat_id);

    /* Escape runtime context for JSON */
    char escaped_ctx[1024];
    int esc_idx2 = 0;
    for (const char *p = runtime_ctx; *p && esc_idx2 < sizeof(escaped_ctx) - 1; p++) {
        if (*p == '"') {
            escaped_ctx[esc_idx2++] = '\\';
            escaped_ctx[esc_idx2++] = '"';
        } else if (*p == '\\') {
            escaped_ctx[esc_idx2++] = '\\';
            escaped_ctx[esc_idx2++] = '\\';
        } else if (*p == '\n') {
            escaped_ctx[esc_idx2++] = '\\';
            escaped_ctx[esc_idx2++] = 'n';
        } else if (*p == '\r') {
            /* skip */
        } else {
            escaped_ctx[esc_idx2++] = *p;
        }
    }
    escaped_ctx[esc_idx2] = '\0';

    /* Escape current message for JSON */
    char escaped_msg[MIMI_CONTEXT_BUF_SIZE * 2];
    int esc_idx3 = 0;
    for (const char *p = current_message; *p && esc_idx3 < sizeof(escaped_msg) - 1; p++) {
        if (*p == '"') {
            escaped_msg[esc_idx3++] = '\\';
            escaped_msg[esc_idx3++] = '"';
        } else if (*p == '\\') {
            escaped_msg[esc_idx3++] = '\\';
            escaped_msg[esc_idx3++] = '\\';
        } else if (*p == '\n') {
            escaped_msg[esc_idx3++] = '\\';
            escaped_msg[esc_idx3++] = 'n';
        } else if (*p == '\r') {
            /* skip */
        } else {
            escaped_msg[esc_idx3++] = *p;
        }
    }
    escaped_msg[esc_idx3] = '\0';

    /* Add user message with runtime context + current message */
    off += snprintf(output_buf + off, output_size - off,
        ",{\"role\":\"user\",\"content\":\"%s\\n\\n%s\"}]",
        escaped_ctx, escaped_msg);

    return ESP_OK;
}
