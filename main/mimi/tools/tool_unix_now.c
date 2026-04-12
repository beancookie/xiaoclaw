#include "tool_unix_now.h"
#include <time.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "tool_unix_now";

esp_err_t tool_unix_now_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    time_t now = time(NULL);
    snprintf(output, output_size, "%lld", (long long)now);
    ESP_LOGI(TAG, "unix_now: %s", output);
    return ESP_OK;
}
