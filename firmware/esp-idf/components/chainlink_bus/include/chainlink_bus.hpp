// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "chainlink_protocol.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace splitflap::chainlink {
struct Config {
    int mosi = -1;
    int miso = -1;
    int clock = -1;
    int latch = -1;
    int output_enable_n = -1; // Not routed on the stock Buddy; do not connect GPIO27.
    std::size_t modules = 6;
    int clock_hz = 4000000;
};
// Pure pin validation: no peripheral/pin is touched, and defaults are unassigned.
esp_err_t validate_config(const Config &config);

// Single-owner, private SPI2 bus; no shared display or concurrent calls.
// app_main deliberately never creates/initializes this interface.
class Bus final {
public:
    Bus() = default;
    ~Bus();
    Bus(const Bus &) = delete;
    Bus &operator=(const Bus &) = delete;
    // The bool is an explicit caller acknowledgement, NOT a sensed interlock.
    esp_err_t initialize(const Config &config, bool motor_supply_disconnected);
    // Raw bytes retain original order. Nonzero motor-coil bits are rejected.
    // On any error sensors are NOT updated and must be treated as invalid.
    esp_err_t exchange_logic(const uint8_t *outputs, std::size_t output_size,
                             uint8_t *sensors, std::size_t sensor_size);
    // No extra latch pulse; leaves optional OE inactive and latch low. Latched
    // outputs can persist: cleanup does not establish physical motor safety.
    esp_err_t close();
private:
    static void latch_high(spi_transaction_t *transaction);
    static void latch_low(spi_transaction_t *transaction);
    esp_err_t fault(const char *stage, esp_err_t error);
    Config config_{};
    TaskHandle_t owner_ = nullptr;
    spi_device_handle_t tx_ = nullptr;
    spi_device_handle_t rx_ = nullptr;
    uint8_t *output_buffer_ = nullptr;
    uint8_t *sensor_buffer_ = nullptr;
    bool bus_owned_ = false;
    bool gpio_owned_ = false;
    bool ready_ = false;
    volatile esp_err_t latch_error_ = ESP_OK;
};
} // namespace splitflap::chainlink
