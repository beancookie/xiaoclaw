#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Memory Hierarchy - L0-L4 layered memory access.
 *
 * Layered memory design inspired by Hermes Agent:
 * - L0: System constraints (SOUL.md - hardcoded)
 * - L1: Skill index (/spiffs/memory/skill_index.json)
 * - L2: User preferences and facts (/spiffs/memory/facts.json or USER.md)
 * - L3: Hot auto-skills (usage_count at least 3)
 * - L4: Archived sessions (/spiffs/sessions/archive/)
 */

/**
 * L1: Get skill index JSON for context.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written
 */
size_t memory_l1_get_skill_index(char *buf, size_t size);

/**
 * L2: Get user facts/preferences.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written
 */
size_t memory_l2_get_facts(char *buf, size_t size);

/**
 * L3: Get hot auto-skills content.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written
 */
size_t memory_l3_get_hot_skills(char *buf, size_t size);

/**
 * L4: Archive a session.
 *
 * @param chat_id  Chat identifier
 * @return ESP_OK on success
 */
esp_err_t memory_l4_archive_session(const char *chat_id);

/**
 * Get the full memory hierarchy summary for debugging.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written
 */
size_t memory_hierarchy_summary(char *buf, size_t size);