#include "context_builder.h"
#include "mimi_config.h"
#include "memory/memory_store.h"
#include "memory/hierarchy.h"
#include "skills/skill_loader.h"
#include "skills/skill_meta.h"
#include "tools/tool_mcp_client.h"

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
    tzset();  // Ensure timezone is set before calling localtime_r
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S CST", &timeinfo);
}

/* ─── Identity Section ───────────────────────────────────────────────────── */

static size_t append_identity(char *buf, size_t size, size_t offset)
{
    return snprintf(buf + offset, size - offset,
        "# XiaoClaw (小龙虾): AI Voice Assistant with Local Agent Brain\n\n"
        "You are XiaoClaw (Chinese: 小龙虾), an AI voice assistant running on an ESP32-S3 device with 32MB Flash and 8MB PSRAM.\n"
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
        "Tool instructions are in skill files under /fatfs/skills/:\n");

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
        "- read_file: Read a file from FATFS.\n"
        "- write_file: Write/overwrite a file.\n"
        "- edit_file: Find-and-replace edit.\n"
        "- list_dir: List files in a directory.\n\n");

    return off;
}

/* ─── Memory Section ──────────────────────────────────────────────────────── */

static size_t append_memory_section(char *buf, size_t size, size_t offset)
{
    char mem_buf[4096];
    size_t off = 0;

    off = snprintf(buf + offset, size - offset,
        "## Memory\n\n"
        "You have persistent memory stored on local flash:\n"
        "- Long-term memory: " MIMI_FATFS_MEMORY_DIR "/MEMORY.md\n\n"
        "IMPORTANT: Actively use memory to remember things across conversations.\n"
        "- When you learn something new about the user (name, preferences, habits, context), write it to MEMORY.md.\n"
        "- Always read_file MEMORY.md before writing, so you can edit_file to update without losing existing content.\n"
        "- Keep MEMORY.md concise and organized — summarize, don't dump raw conversation.\n"
        "- You should proactively save memory without being asked. If the user tells you their name, preferences, or important facts, persist them immediately.\n\n");

    /* Memory L0-L4 Hierarchy */
    off += snprintf(buf + offset + off, size - offset - off,
        "## Memory Hierarchy (L0-L4)\n\n"
        "| Layer | Content | Storage | Notes |\n"
        "|-------|---------|---------|-------|\n"
        "| L0 | System constraints | Hardcoded (SOUL.md) | Base rules |\n"
        "| L1 | Skill index | " MIMI_FATFS_MEMORY_DIR "/skill_index.json | Auto-updated |\n"
        "| L2 | User facts/preferences | " MIMI_FATFS_MEMORY_DIR "/MEMORY.md | Long-term |\n"
        "| L3 | Hot auto-skills (usage>=3) | /fatfs/skills/auto/ | Auto-loaded |\n"
        "| L4 | Archived sessions | /fatfs/sessions/archive/ | Summarized |\n\n"
        "**Memory Workflow:**\n"
        "1. Read: read_file `" MIMI_FATFS_MEMORY_DIR "/MEMORY.md`\n"
        "2. Update: edit_file (find-and-replace) — NEVER write_file directly\n"
        "3. Save user facts proactively — name, preferences, habits\n\n"
        "**Important:** Always read_file before edit_file to avoid losing content.\n\n");

    /* Long-term memory */
    if (memory_read_long_term(mem_buf, sizeof(mem_buf)) == ESP_OK && mem_buf[0]) {
        off += snprintf(buf + offset + off, size - offset - off, "## Long-term Memory\n\n%s\n\n", mem_buf);
    }

    return off;
}

/* ─── Skills Section ─────────────────────────────────────────────────────── */

static size_t append_skills_section(char *buf, size_t size, size_t offset)
{
    size_t off = 0;

    /* L1: Skill index - available skills to CALL when needed */
    char l1_index[2048];
    size_t l1_len = skill_meta_get_all_json(l1_index, sizeof(l1_index));
    if (l1_len > 0) {
        off += snprintf(buf + offset + off, size - offset - off,
            "## Available Skills (TOOLS - call directly, do not just describe)\n\n"
            "```json\n%s\n```\n\n"
            "**Call format:** `{\"name\": \"auto_xxxxxxxx\"}`\n"
            "**Important:** Call skills directly, do NOT just list/summarize them!\n\n",
            l1_index);
    }

    /* L2: User facts/preferences (if available) */
    char l2_facts[1024];
    size_t l2_len = memory_get_facts(l2_facts, sizeof(l2_facts));
    if (l2_len > 0) {
        off += snprintf(buf + offset + off, size - offset - off,
            "## User Facts (L2)\n\n%s\n\n", l2_facts);
    }

    /* L3: Hot auto-skills (only is_hot=true, full content) */
    char l3_hot[4096];
    size_t l3_len = skill_meta_get_hot_skills(l3_hot, sizeof(l3_hot));
    if (l3_len > 0) {
        off += snprintf(buf + offset + off, size - offset - off,
            "## Active Auto-Skills (L3)\n\n%s\n\n", l3_hot);
    }

    /* Always skills (always loaded, full content) */
    char always_content[8192];
    size_t always_len = skill_loader_get_always_content(always_content, sizeof(always_content));
    if (always_len > 0) {
        off += snprintf(buf + offset + off, size - offset - off,
            "## Always-Active Skills\n\n%s\n\n", always_content);
    }

    /* Skills summary (for reference) */
    char skills_summary[2048];
    size_t summary_len = skill_loader_build_summary(skills_summary, sizeof(skills_summary));
    if (summary_len > 0) {
        off += snprintf(buf + offset + off, size - offset - off,
            "## Available Skills (read full instructions with read_file when needed)\n\n%s\n",
            skills_summary);
    }

    /* Skill Usage Workflow - emphasize calling, not describing */
    off += snprintf(buf + offset + off, size - offset - off,
        "## Skill Usage\n\n"
        "**When a task matches a known skill:**\n"
        "1. Call the skill tool directly by name (e.g., `auto_97154022`)\n"
        "2. Do NOT summarize skill lists - use them!\n\n"
        "**File tools:**\n"
        "- list_dir: `{\"prefix\": \"/fatfs/skills\"}` - discover skill files\n"
        "- read_file: `{\"path\": \"/fatfs/skills/.../SKILL.md\"}` - read skill content\n\n"
        "Paths must start with `/fatfs/` - `..` is blocked.\n\n");

    /* Skill Auto-Crystallization */
    off += snprintf(buf + offset + off, size - offset - off,
        "## Skill Auto-Crystallization\n\n"
        "When a multi-step task succeeds, you can trigger automatic skill creation:\n\n"
        "**Crystallization Conditions (all must be met):**\n"
        "- Task completed successfully\n"
        "- At least 2 steps were required\n"
        "- No similar skill already exists\n\n"
        "**Crystallized skills are saved to:**\n"
        "`/fatfs/skills/auto/auto_<name>_<hash>/SKILL.md`\n\n"
        "**To trigger crystallization:**\n"
        "Complete the multi-step task successfully. The system will:\n"
        "1. Detect success + multiple steps\n"
        "2. Check for similar existing skill\n"
        "3. Auto-create skill in /fatfs/skills/auto/\n\n"
        "**Skill Metadata Tracking:**\n"
        "- usage_count: Increments each time skill is used\n"
        "- success_count: Increments on successful use\n"
        "- is_hot: True when usage_count >= 3 (full content auto-loaded)\n\n");

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

    /* MCP Configuration (if enabled) */
#ifdef CONFIG_MIMI_MCP_CLIENT_ENABLE
    if (strlen(CONFIG_MIMI_MCP_REMOTE_HOST) > 0) {
        off += snprintf(buf + off, size - off,
            "\n## MCP Server Configuration\n\n"
            "A remote MCP server is configured and available:\n"
            "- Host: " CONFIG_MIMI_MCP_REMOTE_HOST "\n"
            "- Port: %d\n"
            "- Endpoint: " CONFIG_MIMI_MCP_REMOTE_EP "\n"
            "- Use mcp_connect tool to connect: {\"server_name\": \"default\"}\n\n",
            CONFIG_MIMI_MCP_REMOTE_PORT);
    }
#endif

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
