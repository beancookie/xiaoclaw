#pragma once

#include <stdbool.h>
#include "esp_err.h"

/**
 * MCP server configuration structure.
 */
typedef struct {
    char name[64];
    char host[128];
    int port;
    char endpoint[64];
    bool auto_connect;  /**< true to connect automatically at startup */
} mcp_server_config_t;

/**
 * Initialize MCP client and discover remote tools.
 * Must be called after WiFi is connected and after tool_registry_init().
 * Auto-connects to servers marked with auto_connect=true in SKILL.md.
 */
esp_err_t tool_mcp_client_init(void);

/**
 * Deinitialize MCP client and release resources.
 */
esp_err_t tool_mcp_client_deinit(void);

/**
 * Check if MCP client is enabled and connected.
 */
bool tool_mcp_client_is_ready(void);

/**
 * Parse SKILL.md and load all MCP server configurations.
 *
 * @param configs  Output array for server configs
 * @param max_count Maximum number of configs to return
 * @param count    Actual number of configs loaded
 * @return ESP_OK on success
 */
esp_err_t mcp_load_all_server_configs(mcp_server_config_t configs[], int max_count, int *count);

/**
 * Connect to an MCP server and register its tools as first-class citizens.
 * Remote tools will be registered with their actual names (e.g., "hue.set_color").
 *
 * @param server_name Server name as defined in mcp-servers/SKILL.md
 * @return ESP_OK on success
 */
esp_err_t mcp_connect_and_register(const char *server_name);
