#include "fatfs_util.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "fatfs_util";

esp_err_t fatfs_write_atomic(const char *path, const void *data, size_t len)
{
    if (!path || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Build temp file path: path + ".tmp" */
    size_t path_len = strlen(path);
    char tmp_path[path_len + 5];  /* +4 for ".tmp" +1 for null */
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    /* Step 1: Create and write to temp file */
    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open temp file for atomic write: %s", tmp_path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, len, f);
    if (written != len) {
        ESP_LOGE(TAG, "Atomic write incomplete: %d/%d bytes", (int)written, (int)len);
        fclose(f);
        remove(tmp_path);
        return ESP_FAIL;
    }

    /* Step 2: Flush C library buffers to FATFS */
    if (fflush(f) != 0) {
        ESP_LOGE(TAG, "Failed to flush: %s", tmp_path);
        fclose(f);
        remove(tmp_path);
        return ESP_FAIL;
    }

    /* Step 3: Close temp file (syncs data to storage) */
    if (fclose(f) != 0) {
        ESP_LOGE(TAG, "Failed to close temp file: %s", tmp_path);
        remove(tmp_path);
        return ESP_FAIL;
    }

    /* Step 4: Remove existing target file (may fail if doesn't exist) */
    remove(path);

    /* Step 5: Atomic rename on same FATFS partition */
    if (rename(tmp_path, path) != 0) {
        ESP_LOGE(TAG, "Failed to rename temp file to: %s", path);
        remove(tmp_path);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Atomic write succeeded: %s (%d bytes)", path, (int)len);
    return ESP_OK;
}
