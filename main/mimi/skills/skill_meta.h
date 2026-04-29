#pragma once

#include "esp_err.h"
#include "mimi_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/**
 * Hot threshold for marking skills as "hot" (loaded in context).
 * A skill with usage_count >= this value is considered hot and
 * its full content is included in the system prompt.
 */
#define SKILL_META_HOT_THRESHOLD 3

/**
 * Maximum number of tags per skill.
 */
#define SKILL_MAX_TAGS 8

/**
 * Maximum number of tools per skill.
 */
#define SKILL_MAX_TOOLS 16

/**
 * Quality score weights for overall quality calculation.
 */
#define SKILL_QUALITY_SCORE(m) \
    ((m).clarity * 0.3 + (m).completeness * 0.3 + (m).actionability * 0.4)

/**
 * Minimum quality score threshold for crystallization.
 * Skills with quality below this are not crystallized.
 */
#define SKILL_QUALITY_THRESHOLD_MIN 30

/**
 * Skill metadata structure for tracking usage and success rates.
 * Stored persistently in /fatfs/memory/skill_index.json
 */
typedef struct {
    char name[64];            // Skill name
    char path[640];           // Full path to SKILL.md file
    bool is_auto;             // true if auto-generated skill
    int usage_count;          // Number of times used
    int success_count;        // Number of successful uses
    float success_rate;       // Calculated: success_count / usage_count
    time_t last_used;         // Unix timestamp of last use
    bool is_hot;              // true if usage_count >= HOT_THRESHOLD

    /* Extended metadata for quality assessment */
    char description[256];           // Detailed description
    char one_line_summary[128];       // One-line summary
    char category[32];               // Category (iot, file_ops, network...)
    char tags[SKILL_MAX_TAGS][32];   // Tags
    int tag_count;                    // Number of tags
    char tools[SKILL_MAX_TOOLS][32]; // Tools used in this skill
    int tool_count;                  // Number of tools

    /* Quality scores (0-100) */
    int clarity;        // Clarity score
    int completeness;  // Completeness score
    int actionability;  // Actionability score
} skill_meta_t;

/**
 * Path to the skill index JSON file (L1 memory layer).
 */
#define SKILL_INDEX_PATH MIMI_FATFS_MEMORY_DIR "/skill_index.json"

/**
 * Maximum number of skills in the index.
 */
#define SKILL_META_MAX_SKILLS 32

/**
 * Initialize the skill metadata system.
 * Loads existing skill_index.json from FATFS if present.
 *
 * @return ESP_OK on success
 */
esp_err_t skill_meta_init(void);

/**
 * Get metadata for a specific skill by name.
 *
 * @param name  Skill name to look up
 * @param meta  Output buffer for metadata (caller allocates)
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND if not in index
 */
esp_err_t skill_meta_get(const char *name, skill_meta_t *meta);

/**
 * Record a skill usage (success or failure).
 * Updates usage_count, success_count, success_rate, last_used.
 * Saves to FATFS after update.
 *
 * @param name    Skill name
 * @param success true if the task succeeded
 * @return ESP_OK on success
 */
esp_err_t skill_meta_record_usage(const char *name, bool success);

/**
 * Update skill metadata (allows agent to modify description, etc).
 *
 * @param name  Skill name
 * @param meta  New metadata values
 * @return ESP_OK on success
 */
esp_err_t skill_meta_update(const char *name, const skill_meta_t *meta);

/**
 * Add a new skill to the index (called when auto-skill is created).
 *
 * @param meta  Skill metadata to add
 * @return ESP_OK on success
 */
esp_err_t skill_meta_add(const skill_meta_t *meta);

/**
 * Get the full skill index as JSON string.
 * Used by context_builder for L1 skill index in system prompt.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written, 0 on error
 */
size_t skill_meta_get_all_json(char *buf, size_t size);

/**
 * Get hot skills (usage_count >= HOT_THRESHOLD).
 * Returns full content for each hot auto-skill.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written, 0 if no hot skills
 */
size_t skill_meta_get_hot_skills(char *buf, size_t size);

/**
 * Get all auto-generated skills (not just hot ones).
 * Returns full content for each auto-skill, allowing LLM to see
 * available skills even before they become hot (usage >= 3).
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written, 0 if no auto skills
 */
size_t skill_meta_get_all_auto_skills(char *buf, size_t size);

/**
 * Save current metadata to FATFS.
 * Called automatically by record_usage and update, but can be
 * called manually to ensure persistence.
 *
 * @return ESP_OK on success
 */
esp_err_t skill_meta_save(void);

/**
 * Check if a similar auto-skill already exists (for crystallization dedup).
 *
 * @param intent  User intent text
 * @return true if a similar skill exists
 */
bool skill_meta_similar_exists(const char *intent);

/**
 * Check if a similar auto-skill exists using LLM semantic understanding.
 * This replaces simple hash-based matching with LLM-based semantic comparison.
 *
 * @param new_intent           User's new task intent
 * @param similar_skill_name   Output: matched skill name (caller allocates, 64 bytes)
 * @param name_size            Size of similar_skill_name buffer
 * @return true if similar skill found
 */
bool skill_meta_similar_exists_llm(const char *new_intent, char *similar_skill_name, size_t name_size);

/**
 * Get overall quality score for a skill.
 *
 * @param meta  Skill metadata
 * @return Overall quality score (0-100)
 */
int skill_meta_get_quality_score(const skill_meta_t *meta);

/**
 * Get list of hot skill names for context building.
 *
 * @param names  Output array of hot skill names (caller allocates)
 * @param max    Maximum number to return
 * @return Actual number of hot skills
 */
int skill_meta_get_hot_names(char names[][64], int max);

/**
 * Find which hot auto-skill matches a tool call.
 * Scans hot auto-skills and checks if the tool name appears in their
 * tool sequence. When found, records usage for that skill.
 *
 * @param tool_name   Name of the tool that was called
 * @param tool_input  JSON input string (used for pattern matching)
 * @param success     Whether the tool call succeeded
 * @return ESP_OK if a matching skill was found and usage recorded,
 *         ESP_ERR_NOT_FOUND if no matching skill
 */
esp_err_t skill_meta_record_skill_usage(const char *tool_name,
                                         const char *tool_input,
                                         bool success);