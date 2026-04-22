#include "memory_store.h"
#include "mimi_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "memory";

/* Path for user facts (L2 memory) */
#define FACTS_FILE MIMI_SPIFFS_MEMORY_DIR "/facts.json"

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

    /* Retry SPIFFS availability check (in case it was still mounting) */
    const int max_retries = 5;
    const int retry_delay_ms = 100;
    esp_err_t last_err = ESP_FAIL;

    for (int attempt = 1; attempt <= max_retries; attempt++) {
        /* Test with existing path check - use stat() to verify directory accessible */
        struct stat st;
        if (stat(MIMI_SPIFFS_MEMORY_DIR, &st) == 0 && S_ISDIR(st.st_mode)) {
            ESP_LOGI(TAG, "Memory store initialized at %s (attempt %d)",
                     MIMI_SPIFFS_BASE, attempt);
            return ESP_OK;
        }
        last_err = ESP_FAIL;
        if (attempt < max_retries) {
            ESP_LOGW(TAG, "SPIFFS not ready, retry %d/%d...", attempt, max_retries);
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        }
    }

    ESP_LOGE(TAG, "SPIFFS not available after %d attempts", max_retries);
    return last_err;
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
    FILE *f = fopen(MIMI_MEMORY_FILE, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot write %s", MIMI_MEMORY_FILE);
        return ESP_FAIL;
    }
    fputs(content, f);
    fclose(f);
    ESP_LOGI(TAG, "Long-term memory updated (%d bytes)", (int)strlen(content));
    return ESP_OK;
}

esp_err_t memory_append_today(const char *note)
{
    char date_str[16];
    get_date_str(date_str, sizeof(date_str), 0);

    char path[64];
    snprintf(path, sizeof(path), "%s/%s.md", MIMI_SPIFFS_MEMORY_DIR, date_str);

    FILE *f = fopen(path, "a");
    if (!f) {
        /* Try creating — if file doesn't exist yet, write header */
        f = fopen(path, "w");
        if (!f) {
            ESP_LOGE(TAG, "Cannot open %s", path);
            return ESP_FAIL;
        }
        fprintf(f, "# %s\n\n", date_str);
    }

    fprintf(f, "%s\n", note);
    fclose(f);
    return ESP_OK;
}

esp_err_t memory_read_recent(char *buf, size_t size, int days)
{
    size_t offset = 0;
    buf[0] = '\0';

    for (int i = 0; i < days && offset < size - 1; i++) {
        char date_str[16];
        get_date_str(date_str, sizeof(date_str), i);

        char path[64];
        snprintf(path, sizeof(path), "%s/%s.md", MIMI_SPIFFS_MEMORY_DIR, date_str);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        if (offset > 0 && offset < size - 4) {
            offset += snprintf(buf + offset, size - offset, "\n---\n");
        }

        size_t n = fread(buf + offset, 1, size - offset - 1, f);
        offset += n;
        buf[offset] = '\0';
        fclose(f);
    }

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
