// SPDX-License-Identifier: Apache-2.0
#include "chainlink_bus.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <set>
#include <vector>

namespace {
using namespace splitflap::chainlink;
std::array<int, 49> levels{};
std::array<gpio_mode_t, 49> modes{};
TaskHandle_t task = reinterpret_cast<void *>(1);
std::set<void *> allocations;
std::set<FakeDevice *> devices;
std::vector<int> transfers;
bool owned = false;
int gpio_changes = 0, allocate_calls = 0, fail_allocate_at = -1;
int add_calls = 0, fail_add_at = -1, fail_mode = -1;
bool fail_remove_once = false;
Config config()
{
    Config c;
#if CONFIG_IDF_TARGET_ESP32S3
    c.mosi = 11; c.miso = 13; c.clock = 12; c.latch = 10;
#else
    c.mosi = 32; c.miso = 39; c.clock = 33; c.latch = 25;
#endif
    c.output_enable_n = 4;
    return c;
}
void clean() { assert(!owned && devices.empty() && allocations.empty()); }
} // namespace
const char *esp_err_to_name(esp_err_t) { return "fake error"; }
TaskHandle_t xTaskGetCurrentTaskHandle() { return task; }
esp_err_t gpio_set_level(gpio_num_t pin, int value)
{
    assert(pin >= 0 && pin < 49); levels[pin] = value; ++gpio_changes; return ESP_OK;
}
esp_err_t gpio_set_direction(gpio_num_t pin, gpio_mode_t mode)
{
    assert(pin >= 0 && pin < 49); modes[pin] = mode; ++gpio_changes; return ESP_OK;
}
esp_err_t gpio_set_pull_mode(gpio_num_t, int) { return ESP_OK; }
void *heap_caps_calloc(std::size_t count, std::size_t size, int caps)
{
    assert(caps == (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (++allocate_calls == fail_allocate_at) return nullptr;
    auto *p = std::calloc(count, size); assert(p); allocations.insert(p); return p;
}
void heap_caps_free(void *p) { if (p) { assert(allocations.erase(p) == 1); std::free(p); } }
esp_err_t spi_bus_initialize(int host, const spi_bus_config_t *c, int dma)
{
    assert(host == SPI2_HOST && dma == SPI_DMA_CH_AUTO && !owned);
    const auto pins = config();
    assert(c->mosi_io_num == pins.mosi && c->miso_io_num == pins.miso && c->sclk_io_num == pins.clock);
    assert(c->quadwp_io_num == -1 && c->quadhd_io_num == -1 && c->data4_io_num == -1);
    assert(levels[pins.latch] == 0 && levels[pins.clock] == 1 && levels[pins.mosi] == 0);
    assert(levels[pins.output_enable_n] == 1 && modes[pins.miso] == GPIO_MODE_INPUT);
    owned = true; return ESP_OK;
}
esp_err_t spi_bus_add_device(int, const spi_device_interface_config_t *c, spi_device_handle_t *handle)
{
    assert(owned && c->spics_io_num == -1 && c->clock_speed_hz == 4000000 && c->queue_size == 1);
    if (++add_calls == fail_add_at) return ESP_ERR_NO_MEM;
    *handle = new FakeDevice{*c}; devices.insert(*handle); return ESP_OK;
}
esp_err_t spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t *t)
{
    const auto &c = device->config;
    transfers.push_back(c.mode);
    if (c.mode == fail_mode) { fail_mode = -1; return ESP_ERR_TIMEOUT; }
    if (c.mode == 3) {
        assert(t->length == 32 && t->tx_buffer && !t->rx_buffer);
        const uint8_t expected[] = {0, 0, 0x80, 0};
        assert(std::memcmp(t->tx_buffer, expected, 4) == 0); // No byte/bit reversal.
        assert(!c.pre_cb && !c.post_cb && c.flags == 0);
    } else {
        assert(c.mode == 2 && t->length == 8 && t->rxlength == 8 && !t->tx_buffer);
        assert(c.input_delay_ns == 30 && c.flags == (SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY));
        c.pre_cb(t); assert(levels[config().latch] == 1);
        std::memset(t->rx_buffer, 0x40, 1);
        c.post_cb(t); assert(levels[config().latch] == 0);
    }
    return ESP_OK;
}
esp_err_t spi_bus_remove_device(spi_device_handle_t device)
{
    if (fail_remove_once) { fail_remove_once = false; return ESP_FAIL; }
    assert(devices.erase(device) == 1); delete device; return ESP_OK;
}
esp_err_t spi_bus_free(int) { assert(owned && devices.empty()); owned = false; return ESP_OK; }

int main()
{
    const auto c = config();
    Bus bus;
    assert(bus.initialize(c, false) == ESP_ERR_INVALID_STATE && gpio_changes == 0);
    assert(bus.initialize(Config{}, true) == ESP_ERR_INVALID_ARG && gpio_changes == 0);
    auto duplicate = c; duplicate.mosi = c.clock;
    assert(bus.initialize(duplicate, true) == ESP_ERR_INVALID_ARG && gpio_changes == 0);
    assert(bus.initialize(c, true) == ESP_OK && transfers.empty());
    assert(bus.initialize(c, true) == ESP_ERR_INVALID_STATE);
    uint8_t out[] = {0, 0, 0x80, 0};
    uint8_t sensor = 0xff;
    assert(bus.exchange_logic(out, 4, &sensor, 1) == ESP_OK && sensor == 0x40);
    assert((transfers == std::vector<int>{3, 2}));
    out[0] = 1;
    assert(bus.exchange_logic(out, 4, &sensor, 1) == ESP_ERR_INVALID_ARG && transfers.size() == 2);
    out[0] = 0;
    task = reinterpret_cast<void *>(2);
    assert(bus.exchange_logic(out, 4, &sensor, 1) == ESP_ERR_INVALID_STATE);
    assert(bus.close() == ESP_ERR_INVALID_STATE);
    task = reinterpret_cast<void *>(1);
    sensor = 0xa5; fail_mode = 2;
    assert(bus.exchange_logic(out, 4, &sensor, 1) == ESP_ERR_TIMEOUT && sensor == 0xa5);
    assert(levels[c.latch] == 0 && levels[c.output_enable_n] == 1);
    assert(bus.exchange_logic(out, 4, &sensor, 1) == ESP_ERR_INVALID_STATE);
    fail_remove_once = true;
    assert(bus.close() == ESP_FAIL && owned && !allocations.empty());
    assert(bus.close() == ESP_OK && bus.close() == ESP_OK); clean();

    fail_allocate_at = allocate_calls + 1;
    assert(bus.initialize(c, true) == ESP_ERR_NO_MEM); clean();
    fail_add_at = add_calls + 2; // Second device failure must remove first device too.
    assert(bus.initialize(c, true) == ESP_ERR_NO_MEM); clean();
    assert(bus.initialize(c, true) == ESP_OK);
    fail_mode = 3; sensor = 0xa5;
    const auto before = transfers.size();
    assert(bus.exchange_logic(out, 4, &sensor, 1) == ESP_ERR_TIMEOUT && sensor == 0xa5);
    assert(transfers.size() == before + 1); // No RX/latch attempt after TX failure.
    assert(bus.close() == ESP_OK); clean();
    std::cout << "PASS: " << (CONFIG_IDF_TARGET_ESP32S3 ? "esp32s3" : "esp32")
              << " HAL fake-driver sequence, inactive startup, rejection, faults and cleanup\n";
}
