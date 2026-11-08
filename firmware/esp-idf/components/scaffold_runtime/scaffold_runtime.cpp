// SPDX-License-Identifier: Apache-2.0
#include "scaffold_runtime.hpp"

#include <cstdint>
#include <cinttypes>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *kTag = "scaffold_runtime";
constexpr uint32_t kStackBytes = 3072; // ESP-IDF task stack sizes are in bytes.
constexpr TickType_t kPeriod = pdMS_TO_TICKS(5000);
ESP_EVENT_DEFINE_BASE(SCAFFOLD_EVENTS);
enum Event : int32_t { Ready, Heartbeat };
struct Health {
    int64_t uptime_ms;
    uint32_t free_heap_bytes;
};
TaskHandle_t supervisor_handle = nullptr;
esp_event_handler_instance_t handler_instance = nullptr;

void on_event(void *, esp_event_base_t, int32_t id, void *data)
{
    if (id == Ready) {
        ESP_LOGI(kTag, "Supervisor ready; no motor, display, serial protocol, or Wi-Fi drivers");
    } else if (id == Heartbeat && data != nullptr) {
        const auto &health = *static_cast<const Health *>(data);
        ESP_LOGI(kTag, "uptime_ms=%" PRId64 " free_heap_bytes=%" PRIu32,
                 health.uptime_ms, health.free_heap_bytes);
    }
}

void post(int32_t id, const void *data, size_t size)
{
    // ESP event loop copies the fixed-size payload. Never block a future producer.
    if (esp_event_post(SCAFFOLD_EVENTS, id, data, size, 0) != ESP_OK) {
        ESP_LOGW(kTag, "Diagnostic event dropped");
    }
}

void supervisor(void *)
{
    post(Ready, nullptr, 0);
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        const Health health{esp_timer_get_time() / 1000, esp_get_free_heap_size()};
        post(Heartbeat, &health, sizeof(health));
        vTaskDelayUntil(&last_wake, kPeriod);
    }
}
} // namespace

namespace splitflap::runtime {
esp_err_t start()
{
    if (supervisor_handle != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = esp_event_handler_instance_register(
        SCAFFOLD_EVENTS, ESP_EVENT_ANY_ID, on_event, nullptr, &handler_instance);
    if (result != ESP_OK) {
        return result;
    }
    if (xTaskCreate(supervisor, "scaffold_health", kStackBytes, nullptr,
                    tskIDLE_PRIORITY + 1, &supervisor_handle) != pdPASS) {
        esp_event_handler_instance_unregister(SCAFFOLD_EVENTS, ESP_EVENT_ANY_ID, handler_instance);
        handler_instance = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
}
