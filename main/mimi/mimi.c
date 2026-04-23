/**
 * @file mimi.c
 * @brief Mimiclaw Agent Engine - Library Mode
 *
 * This is a modified version of mimiclaw designed to be embedded
 * into xiaozhi as an Agent engine. The original app_main() has been
 * converted to mimiclaw_init() for library usage.
 *
 * Key changes from standalone mimiclaw:
 * - WiFi management removed (handled by xiaozhi)
 * - External channels (Telegram/Feishu/WS Server) disabled
 * - NVS/SPIFFS init moved to caller responsibility
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "mimi_config.h"
#include "bus/message_bus.h"
#include "llm/llm_proxy.h"
#include "agent/agent_loop.h"
#include "memory/memory_store.h"
#include "memory/session_manager.h"
#include "proxy/http_proxy.h"
#include "tools/tool_registry.h"
#include "tools/tool_mcp_client.h"
#include "skills/skill_loader.h"
#include "skills/skill_meta.h"
#include "skills/skill_crystallize.h"

// Disabled for embedded mode (xiaozhi handles these):
// #include "channels/telegram/telegram_bot.h"
// #include "channels/feishu/feishu_bot.h"
// #include "gateway/ws_server.h"
#include "cron/cron_service.h"
#include "heartbeat/heartbeat.h"

static const char *TAG = "mimi";

static esp_err_t init_spiffs(void)
{
    /* Check if SPIFFS already mounted (e.g., by xiaozhi host) */
    size_t total = 0, used = 0;
    if (esp_spiffs_info("spiffs", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS already mounted, reusing: total=%d, used=%d", (int)total, (int)used);
        return ESP_OK;
    }

    /* Mount SPIFFS if not already mounted */
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MIMI_SPIFFS_BASE,
        .partition_label = "spiffs",
        .max_files = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_spiffs_info("spiffs", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: total=%d, used=%d", (int)total, (int)used);

    return ESP_OK;
}

/**
 * @brief Initialize mimiclaw Agent engine
 *
 * This function initializes all mimiclaw subsystems for use as an
 * embedded Agent engine. Call this after:
 * - NVS is initialized
 * - Default event loop is created
 * - WiFi is connected (for LLM API access)
 * - SPIFFS partition is available
 *
 * @return ESP_OK on success
 */
esp_err_t mimiclaw_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Mimiclaw Agent Engine (Embedded)");
    ESP_LOGI(TAG, "========================================");

    /* Print memory info */
    ESP_LOGI(TAG, "Internal free: %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "PSRAM free:    %d bytes",
             (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Initialize SPIFFS (for memory/sessions/skills storage) */
    ESP_ERROR_CHECK(init_spiffs());

    /* Initialize core subsystems */
    ESP_ERROR_CHECK(message_bus_init());
    ESP_ERROR_CHECK(memory_store_init());
    ESP_ERROR_CHECK(skill_loader_init());
    ESP_ERROR_CHECK(skill_crystallize_init());
    ESP_ERROR_CHECK(session_manager_init());
    ESP_ERROR_CHECK(http_proxy_init());
    ESP_ERROR_CHECK(llm_proxy_init());
    ESP_ERROR_CHECK(tool_registry_init());
    ESP_ERROR_CHECK(tool_mcp_client_init());
    ESP_ERROR_CHECK(cron_service_init());
    ESP_ERROR_CHECK(cron_service_start());
    ESP_ERROR_CHECK(heartbeat_init());
    ESP_ERROR_CHECK(agent_loop_init());

    /* Start Agent Loop on Core 1 */
    ESP_ERROR_CHECK(agent_loop_start());

    ESP_LOGI(TAG, "Mimiclaw Agent Engine initialized successfully");
    return ESP_OK;
}

/**
 * @brief Start mimiclaw Agent engine (alias for compatibility)
 *
 * @return ESP_OK on success
 */
esp_err_t mimiclaw_start(void)
{
    return mimiclaw_init();
}

/**
 * @brief Check if mimiclaw is ready to process messages
 *
 * @return true if Agent Loop is running
 */
bool mimiclaw_is_ready(void)
{
    return true;  // TODO: Add actual state tracking
}
