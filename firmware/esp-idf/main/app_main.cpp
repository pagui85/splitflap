// SPDX-License-Identifier: Apache-2.0
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "scaffold_platform.hpp"
#include "scaffold_runtime.hpp"
#include "sdkconfig.h"

extern "C" void app_main()
{
    constexpr const char *tag = "splitflap_scaffold";
    ESP_LOGI(tag, "Native ESP-IDF scaffold; hardware and networking are not started");
    ESP_LOGI(tag, "Target %s; Chainlink SPI/GPIO interface is compiled but not initialized",
             CONFIG_IDF_TARGET);

    const esp_err_t result = splitflap::platform::initialize_storage();
    if (result != ESP_OK) {
        ESP_LOGE(tag, "Storage initialization failed (%s); stopping without erasing NVS",
                 esp_err_to_name(result));
        return;
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(splitflap::runtime::start());
    // The IDF main task may return. The supervisor and event loop own their tasks.
}
