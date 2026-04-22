#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/**
 * Skill information structure.
 */
typedef struct {
    char name[32];
    char description[128];
    bool always;           /**< true if always loaded in system prompt */
    bool available;        /**< true if requirements are met */
    char path[256];        /**< Full path to SKILL.md file */
    char source;           /**< 'w' for workspace, 'b' for builtin */
    /* Metadata from skill_meta (L1 index) */
    int usage_count;       /**< Number of times used (from skill_index.json) */
    float success_rate;    /**< Success rate (from skill_index.json) */
    time_t last_used;      /**< Last usage timestamp */
} skill_info_t;

/**
 * Initialize skills system.
 * Scans SPIFFS for available skill markdown files.
 */
esp_err_t skill_loader_init(void);

/**
 * Build a summary of all available skills for the system prompt.
 * Uses XML format to list each skill with name, description, and availability.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written (0 if no skills found)
 */
size_t skill_loader_build_summary(char *buf, size_t size);

/**
 * List all available skills.
 *
 * @param skills Output array (caller allocates)
 * @param max    Maximum number of skills to return
 * @return Actual number of skills found
 */
int skill_loader_list(skill_info_t *skills, int max);

/**
 * Load a skill by name.
 *
 * @param name  Skill name (directory name)
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if skill not found
 */
esp_err_t skill_loader_load(const char *name, char *buf, size_t size);

/**
 * Get content of all "always" skills (separated by ---).
 * These skills are always loaded in the system prompt.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written
 */
size_t skill_loader_get_always_content(char *buf, size_t size);

/**
 * Get content of "hot" auto skills (usage_count >= 3).
 * Used by context_builder to load hot skills in L3.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written
 */
size_t skill_loader_get_hot_skills_content(char *buf, size_t size);

/**
 * Check if a skill's requirements are met.
 *
 * @param name  Skill name
 * @return true if requirements are met or no requirements
 */
bool skill_loader_check_requirements(const char *name);

/**
 * Get MCP server configuration by server name.
 * Parses mcp-servers.md skill file and extracts server config.
 *
 * @param server_name  Server identifier (section name in skill file)
 * @param host         Output buffer for host string
 * @param host_size    Host buffer size
 * @param port         Output pointer for port number
 * @param endpoint     Output buffer for endpoint string
 * @param ep_size      Endpoint buffer size
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND if server doesn't exist
 */
esp_err_t skill_loader_get_mcp_server_config(const char *server_name,
                                               char *host, size_t host_size,
                                               int *port, char *endpoint, size_t ep_size);
