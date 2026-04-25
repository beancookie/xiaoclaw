#include "checkpoint.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_err.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "checkpoint";

#define CHECKPOINT_FILE_PREFIX "/fatfs/sessions/.checkpoint."

static const char *phase_to_string(checkpoint_phase_t phase)
{
    switch (phase) {
        case CHECKPOINT_PHASE_STARTED:        return "started";
        case CHECKPOINT_PHASE_AWAITING_TOOLS:  return "awaiting_tools";
        case CHECKPOINT_PHASE_TOOLS_DONE:      return "tools_done";
        case CHECKPOINT_PHASE_FINAL_RESPONSE:  return "final_response";
        default:                               return "unknown";
    }
}

static checkpoint_phase_t phase_from_string(const char *str)
{
    if (strcmp(str, "started") == 0)         return CHECKPOINT_PHASE_STARTED;
    if (strcmp(str, "awaiting_tools") == 0)  return CHECKPOINT_PHASE_AWAITING_TOOLS;
    if (strcmp(str, "tools_done") == 0)      return CHECKPOINT_PHASE_TOOLS_DONE;
    if (strcmp(str, "final_response") == 0)   return CHECKPOINT_PHASE_FINAL_RESPONSE;
    return CHECKPOINT_PHASE_STARTED;
}

static void get_checkpoint_path(const char *chat_id, char *path, size_t path_size)
{
    snprintf(path, path_size, "%s%s.json", CHECKPOINT_FILE_PREFIX, chat_id);
}

esp_err_t checkpoint_save(const char *chat_id, checkpoint_phase_t phase,
                         int iteration, const cJSON *checkpoint)
{
    if (!chat_id || !checkpoint) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[256];
    get_checkpoint_path(chat_id, path, sizeof(path));

    /* Build checkpoint JSON with metadata */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "phase", phase_to_string(phase));
    cJSON_AddNumberToObject(root, "iteration", iteration);
    cJSON_AddNumberToObject(root, "saved_at", time(NULL));
    cJSON_AddItemReferenceToObject(root, "data", (cJSON *)checkpoint);  /* Reference, not copy */

    /* Write to file */
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open checkpoint file: %s", path);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        fputs(json_str, f);
        free(json_str);
    }
    fclose(f);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Checkpoint saved for %s: phase=%s iter=%d", chat_id, phase_to_string(phase), iteration);
    return ESP_OK;
}

esp_err_t checkpoint_load(const char *chat_id, checkpoint_phase_t *phase,
                         int *iteration, cJSON **checkpoint)
{
    if (!chat_id || !phase || !iteration || !checkpoint) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[256];
    get_checkpoint_path(chat_id, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }

    /* Read file content */
    struct stat st;
    if (fstat(fileno(f), &st) != 0) {
        fclose(f);
        return ESP_FAIL;
    }

    char *content = malloc(st.st_size + 1);
    if (!content) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t n = fread(content, 1, st.st_size, f);
    content[n] = '\0';
    fclose(f);

    /* Parse JSON */
    cJSON *root = cJSON_Parse(content);
    free(content);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse checkpoint JSON");
        return ESP_FAIL;
    }

    cJSON *phase_str = cJSON_GetObjectItem(root, "phase");
    cJSON *iter_item = cJSON_GetObjectItem(root, "iteration");
    cJSON *data = cJSON_GetObjectItem(root, "data");

    if (phase_str && cJSON_IsString(phase_str)) {
        *phase = phase_from_string(phase_str->valuestring);
    } else {
        *phase = CHECKPOINT_PHASE_STARTED;
    }

    *iteration = iter_item ? (int)iter_item->valuedouble : 0;

    if (data) {
        *checkpoint = cJSON_Duplicate(data, true);
    } else {
        *checkpoint = cJSON_CreateObject();
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Checkpoint loaded for %s: phase=%s iter=%d", chat_id, phase_to_string(*phase), *iteration);
    return ESP_OK;
}

esp_err_t checkpoint_clear(const char *chat_id)
{
    if (!chat_id) {
        return ESP_ERR_INVALID_ARG;
    }

    char path[256];
    get_checkpoint_path(chat_id, path, sizeof(path));

    if (remove(path) == 0) {
        ESP_LOGI(TAG, "Checkpoint cleared for %s", chat_id);
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

bool checkpoint_exists(const char *chat_id)
{
    if (!chat_id) {
        return false;
    }

    char path[256];
    get_checkpoint_path(chat_id, path, sizeof(path));

    struct stat st;
    return stat(path, &st) == 0;
}
