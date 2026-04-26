#include "memory_store.h"
#include "mimi_config.h"
#include "util/fatfs_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "memory";

/* Path for user facts (L2 memory) */
#define FACTS_FILE MIMI_FATFS_MEMORY_DIR "/facts.json"

static void get_date_str(char *buf, size_t size, int days_ago)
{
    time_t now;
    time(&now);
    now -= days_ago * 86400;
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, size, "%Y-%m-%d", &tm);
}

esp_err_t memory_store_init(void)
{
    /* Initialize timezone once at startup - TZ environment variable
       is process-wide and tzset() is expensive, so we only call it once */
    setenv("TZ", MIMI_TIMEZONE, 1);
    tzset();

    /* Check FATFS availability via stat on base directory */
    struct stat s;
    if (stat(MIMI_FATFS_BASE, &s) == 0) {
        ESP_LOGI(TAG, "Memory store ready (FATFS at %s)", MIMI_FATFS_BASE);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "FATFS not available at %s", MIMI_FATFS_BASE);
    return ESP_FAIL;
}

esp_err_t memory_read_long_term(char *buf, size_t size)
{
    FILE *f = fopen(MIMI_MEMORY_FILE, "r");
    if (!f) {
        buf[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }

    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return ESP_OK;
}

esp_err_t memory_write_long_term(const char *content)
{
    esp_err_t err = fatfs_write_atomic(MIMI_MEMORY_FILE, content, strlen(content));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to atomically write %s", MIMI_MEMORY_FILE);
        return err;
    }
    ESP_LOGI(TAG, "Long-term memory updated (%d bytes)", (int)strlen(content));
    return ESP_OK;
}

esp_err_t memory_get_facts(char *buf, size_t size)
{
    FILE *f = fopen(FACTS_FILE, "r");
    if (!f) {
        /* Try USER.md as fallback */
        f = fopen(MIMI_USER_FILE, "r");
        if (!f) {
            buf[0] = '\0';
            return ESP_ERR_NOT_FOUND;
        }
    }

    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return ESP_OK;
}
