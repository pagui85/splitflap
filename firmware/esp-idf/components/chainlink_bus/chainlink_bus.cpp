// SPDX-License-Identifier: Apache-2.0
// SPI modes/layout follow Scott Bezek and contributors' spi_io_config.h.
#include "chainlink_bus.hpp"
#include "chainlink_pin_policy.hpp"
#include <cstring>
#include <initializer_list>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace splitflap::chainlink {
namespace {
constexpr auto kHost = SPI2_HOST;
constexpr const char *kTag = "chainlink_bus";
bool valid_pin(int pin, bool output)
{
    if (!GPIO_IS_VALID_GPIO(pin) || (output && !GPIO_IS_VALID_OUTPUT_GPIO(pin))) return false;
#if CONFIG_IDF_TARGET_ESP32P4
    return allowed_pin(Chip::Esp32P4, pin, output);
#elif CONFIG_IDF_TARGET_ESP32S3
    return allowed_pin(Chip::Esp32S3, pin, output);
#else
    return allowed_pin(Chip::Esp32, pin, output);
#endif
}
esp_err_t drive(int pin, int level)
{
    const auto p = static_cast<gpio_num_t>(pin);
    // Set output latch before changing direction; external bias/interlock is
    // still required for reset, power sequencing and a crashed processor.
    esp_err_t result = gpio_set_level(p, level);
    if (result == ESP_OK) result = gpio_set_direction(p, GPIO_MODE_OUTPUT);
    if (result == ESP_OK) result = gpio_set_pull_mode(p, GPIO_FLOATING);
    return result;
}
} // namespace

esp_err_t validate_config(const Config &c)
{
    if (!valid_capacity(c.modules) || c.clock_hz < 100000 || c.clock_hz > 4000000)
        return ESP_ERR_INVALID_ARG;
    const int pins[] = {c.mosi, c.miso, c.clock, c.latch, c.output_enable_n};
    for (std::size_t i = 0; i < 5; ++i) {
        if (i == 4 && pins[i] == -1) continue;
        if (!valid_pin(pins[i], i != 1)) return ESP_ERR_INVALID_ARG;
        for (std::size_t j = 0; j < i; ++j) if (pins[i] == pins[j]) return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

Bus::~Bus() { if (owner_ != nullptr) ESP_ERROR_CHECK(close()); }

esp_err_t Bus::fault(const char *stage, esp_err_t error)
{
    ready_ = false;
    if (config_.output_enable_n >= 0)
        gpio_set_level(static_cast<gpio_num_t>(config_.output_enable_n), 1);
    if (gpio_owned_) gpio_set_level(static_cast<gpio_num_t>(config_.latch), 0);
    ESP_LOGE(kTag, "%s failed: %s; interface faulted, sensor data invalid", stage, esp_err_to_name(error));
    return error;
}

esp_err_t Bus::initialize(const Config &c, bool motor_supply_disconnected)
{
    if (owner_ != nullptr) return ESP_ERR_INVALID_STATE;
    if (!motor_supply_disconnected) return ESP_ERR_INVALID_STATE;
    esp_err_t result = validate_config(c);
    if (result != ESP_OK) return result; // All validation precedes GPIO changes.
    config_ = c;
    owner_ = xTaskGetCurrentTaskHandle();
    gpio_owned_ = true;
    if (c.output_enable_n >= 0) result = drive(c.output_enable_n, 1);
    if (result == ESP_OK) result = drive(c.latch, 0);
    if (result == ESP_OK) result = drive(c.clock, 1); // Both modes idle high.
    if (result == ESP_OK) result = drive(c.mosi, 0);
    if (result == ESP_OK) result = gpio_set_direction(static_cast<gpio_num_t>(c.miso), GPIO_MODE_INPUT);
    if (result == ESP_OK) result = gpio_set_pull_mode(static_cast<gpio_num_t>(c.miso), GPIO_FLOATING);
    if (result != ESP_OK) { fault("GPIO setup", result); close(); return result; }

    output_buffer_ = static_cast<uint8_t *>(heap_caps_calloc(output_bytes(c.modules), 1, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    const std::size_t rx_allocation = (sensor_bytes(c.modules) + 3) & ~std::size_t{3};
    sensor_buffer_ = static_cast<uint8_t *>(heap_caps_calloc(rx_allocation, 1, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!output_buffer_ || !sensor_buffer_) {
        fault("DMA buffer allocation", ESP_ERR_NO_MEM); close(); return ESP_ERR_NO_MEM;
    }
    spi_bus_config_t bus{};
    bus.mosi_io_num = c.mosi;
    bus.miso_io_num = c.miso;
    bus.sclk_io_num = c.clock;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
    bus.max_transfer_sz = static_cast<int>(output_bytes(c.modules));
    result = spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) { fault("SPI bus setup", result); close(); return result; }
    bus_owned_ = true;
    spi_device_interface_config_t device{};
    device.mode = 3;
    device.clock_speed_hz = c.clock_hz;
    device.spics_io_num = -1; // The shared latch is not ordinary per-device CS.
    device.queue_size = 1;
    result = spi_bus_add_device(kHost, &device, &tx_);
    if (result == ESP_OK) {
        device.mode = 2;
        device.input_delay_ns = 30;
        // No extra clocks: preserve legacy shift register byte/bit alignment.
        device.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
        device.pre_cb = latch_high;
        device.post_cb = latch_low;
        result = spi_bus_add_device(kHost, &device, &rx_);
    }
    if (result != ESP_OK) { fault("SPI device setup", result); close(); return result; }
    ready_ = true;
    // No transfer/latch pulse and no OE assertion during initialization.
    ESP_LOGI(kTag, "Logic-only interface initialized; 12V must remain disconnected");
    return ESP_OK;
}

void IRAM_ATTR Bus::latch_high(spi_transaction_t *t)
{
    auto *self = static_cast<Bus *>(t->user);
    if (gpio_set_level(static_cast<gpio_num_t>(self->config_.latch), 1) != ESP_OK)
        self->latch_error_ = ESP_FAIL;
}
void IRAM_ATTR Bus::latch_low(spi_transaction_t *t)
{
    auto *self = static_cast<Bus *>(t->user);
    if (gpio_set_level(static_cast<gpio_num_t>(self->config_.latch), 0) != ESP_OK)
        self->latch_error_ = ESP_FAIL;
}

esp_err_t Bus::exchange_logic(const uint8_t *out, std::size_t out_size,
                              uint8_t *sensors, std::size_t in_size)
{
    if (!ready_ || owner_ != xTaskGetCurrentTaskHandle()) return ESP_ERR_INVALID_STATE;
    if (!sensors || in_size != sensor_bytes(config_.modules) ||
        !coils_are_off(out, out_size, config_.modules)) return ESP_ERR_INVALID_ARG;
    std::memcpy(output_buffer_, out, out_size);
    spi_transaction_t tx{};
    tx.length = out_size * 8;
    tx.tx_buffer = output_buffer_;
    esp_err_t result = spi_device_polling_transmit(tx_, &tx);
    if (result != ESP_OK) return fault("mode-3 output shift", result);
    spi_transaction_t rx{};
    rx.length = in_size * 8;
    rx.rxlength = in_size * 8;
    rx.rx_buffer = sensor_buffer_;
    rx.user = this;
    latch_error_ = ESP_OK;
    result = spi_device_polling_transmit(rx_, &rx);
    if (result != ESP_OK) return fault("mode-2 sensor shift", result);
    if (latch_error_ != ESP_OK) return fault("sensor latch callbacks", latch_error_);
    std::memcpy(sensors, sensor_buffer_, in_size);
    return ESP_OK;
}

esp_err_t Bus::close()
{
    if (owner_ == nullptr) return ESP_OK;
    if (owner_ != xTaskGetCurrentTaskHandle()) return ESP_ERR_INVALID_STATE;
    ready_ = false;
    esp_err_t first = ESP_OK;
    const auto keep_error = [&first](esp_err_t r) { if (first == ESP_OK && r != ESP_OK) first = r; };
    if (config_.output_enable_n >= 0)
        keep_error(gpio_set_level(static_cast<gpio_num_t>(config_.output_enable_n), 1));
    if (gpio_owned_) keep_error(gpio_set_level(static_cast<gpio_num_t>(config_.latch), 0));
    if (rx_) { const auto r = spi_bus_remove_device(rx_); keep_error(r); if (r == ESP_OK) rx_ = nullptr; }
    if (tx_) { const auto r = spi_bus_remove_device(tx_); keep_error(r); if (r == ESP_OK) tx_ = nullptr; }
    if (!rx_ && !tx_ && bus_owned_) {
        const auto r = spi_bus_free(kHost); keep_error(r); if (r == ESP_OK) bus_owned_ = false;
    }
    // Retain resources on a failed driver release, so explicit close can retry.
    if (!bus_owned_ && !rx_ && !tx_) {
        if (gpio_owned_) {
            for (int pin : {config_.mosi, config_.miso, config_.clock})
                keep_error(gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT));
        }
        heap_caps_free(output_buffer_); output_buffer_ = nullptr;
        heap_caps_free(sensor_buffer_); sensor_buffer_ = nullptr;
        gpio_owned_ = false;
        owner_ = nullptr;
    }
    if (first != ESP_OK) ESP_LOGE(kTag, "Cleanup failed: %s", esp_err_to_name(first));
    return first;
}
} // namespace splitflap::chainlink
