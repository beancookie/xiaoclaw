#include "bridge.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

extern "C" {
#include "mimi/bus/message_bus.h"
}

static const char *TAG = "bridge";

// Channel identifier for xiaozhi
#define BRIDGE_CHANNEL_XIAOZHI  "xiaozhi"
#define BRIDGE_CHAT_ID_LOCAL    "local"

// Static callback for Agent responses
static bridge_response_cb_t s_response_callback = nullptr;

// Bridge task handle
static TaskHandle_t s_bridge_task_handle = nullptr;

// Bridge task stack size
#define BRIDGE_TASK_STACK_SIZE  4096
#define BRIDGE_TASK_PRIORITY    5
#define BRIDGE_TASK_CORE        0

esp_err_t bridge_init(void)
{
    ESP_LOGI(TAG, "Bridge layer initialized");
    return ESP_OK;
}

esp_err_t bridge_send_to_agent(const char *text)
{
    if (text == nullptr) {
        ESP_LOGW(TAG, "bridge_send_to_agent: text is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    mimi_msg_t msg = {0};

    // Set channel to xiaozhi
    strncpy(msg.channel, BRIDGE_CHANNEL_XIAOZHI, sizeof(msg.channel) - 1);
    msg.channel[sizeof(msg.channel) - 1] = '\0';

    // Set chat_id to local
    strncpy(msg.chat_id, BRIDGE_CHAT_ID_LOCAL, sizeof(msg.chat_id) - 1);
    msg.chat_id[sizeof(msg.chat_id) - 1] = '\0';

    // Duplicate the text content (message_bus takes ownership)
    msg.content = strdup(text);
    if (msg.content == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for message content");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = message_bus_push_inbound(&msg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to push message to inbound queue: %s", esp_err_to_name(err));
        free(msg.content);
        return err;
    }

    ESP_LOGI(TAG, "Sent message to Agent: %.60s...", text);
    return ESP_OK;
}

void bridge_task(void *arg)
{
    ESP_LOGI(TAG, "Bridge task started on core %d", xPortGetCoreID());

    while (1) {
        mimi_msg_t msg;

        // Wait for messages from the outbound queue
        esp_err_t err = message_bus_pop_outbound(&msg, portMAX_DELAY);

        if (err != ESP_OK) {
            continue;
        }

        // Only process messages destined for xiaozhi channel
        if (strcmp(msg.channel, BRIDGE_CHANNEL_XIAOZHI) == 0) {
            ESP_LOGI(TAG, "Received Agent response: %.60s...", msg.content ? msg.content : "(null)");

            if (s_response_callback != nullptr && msg.content != nullptr) {
                // Invoke the callback with the response
                s_response_callback(msg.content);
            }
        }

        // Free the message content
        if (msg.content != nullptr) {
            free(msg.content);
        }
    }
}

void bridge_set_response_callback(bridge_response_cb_t callback)
{
    s_response_callback = callback;
    ESP_LOGI(TAG, "Response callback %s", callback ? "registered" : "unregistered");
}

esp_err_t bridge_start(void)
{
    if (s_bridge_task_handle != nullptr) {
        ESP_LOGW(TAG, "Bridge task already running");
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        bridge_task,
        "bridge",
        BRIDGE_TASK_STACK_SIZE,
        nullptr,
        BRIDGE_TASK_PRIORITY,
        &s_bridge_task_handle,
        BRIDGE_TASK_CORE
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create bridge task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Bridge task created successfully");
    return ESP_OK;
}
