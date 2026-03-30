#pragma once

#include "esp_err.h"

/**
 * Initialize MCP client and discover remote tools.
 * Must be called after WiFi is connected and after tool_registry_init().
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
