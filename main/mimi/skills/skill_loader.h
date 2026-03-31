#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Initialize skills system.
 * Scans SPIFFS for available skill markdown files.
 */
esp_err_t skill_loader_init(void);

/**
 * Build a summary of all available skills for the system prompt.
 * Lists each skill with its title and description.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return Number of bytes written (0 if no skills found)
 */
size_t skill_loader_build_summary(char *buf, size_t size);

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
