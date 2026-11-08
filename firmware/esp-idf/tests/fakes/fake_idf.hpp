// SPDX-License-Identifier: Apache-2.0
// Minimal host model, NOT an electrical/peripheral emulator or an IDF replacement.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = 1,
    ESP_ERR_INVALID_STATE = 2, ESP_ERR_NO_MEM = 3, ESP_ERR_TIMEOUT = 4;
#define ESP_ERROR_CHECK(expr) do { if ((expr) != ESP_OK) std::abort(); } while (0)
#define IRAM_ATTR
template <typename... T> void fake_log(T&&...) {}
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
const char *esp_err_to_name(esp_err_t);
using TaskHandle_t = void *;
TaskHandle_t xTaskGetCurrentTaskHandle();
using gpio_num_t = int;
enum gpio_mode_t { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT };
constexpr int GPIO_FLOATING = 0;
#define GPIO_IS_VALID_GPIO(p) ((p) >= 0 && (p) <= (CONFIG_IDF_TARGET_ESP32S3 ? 48 : 39))
#define GPIO_IS_VALID_OUTPUT_GPIO(p) (GPIO_IS_VALID_GPIO(p) && (CONFIG_IDF_TARGET_ESP32S3 || (p) < 34))
esp_err_t gpio_set_level(gpio_num_t, int);
esp_err_t gpio_set_direction(gpio_num_t, gpio_mode_t);
esp_err_t gpio_set_pull_mode(gpio_num_t, int);
constexpr int SPI2_HOST = 1, SPI_DMA_CH_AUTO = 3, SPI_DEVICE_HALFDUPLEX = 1, SPI_DEVICE_NO_DUMMY = 2;
constexpr int MALLOC_CAP_DMA = 1, MALLOC_CAP_INTERNAL = 2;
void *heap_caps_calloc(std::size_t, std::size_t, int);
void heap_caps_free(void *);
struct spi_transaction_t {
    std::size_t length = 0, rxlength = 0;
    const void *tx_buffer = nullptr;
    void *rx_buffer = nullptr;
    void *user = nullptr;
};
struct spi_bus_config_t {
    int mosi_io_num = 0, miso_io_num = 0, sclk_io_num = 0;
    int quadwp_io_num = 0, quadhd_io_num = 0;
    int data4_io_num = 0, data5_io_num = 0, data6_io_num = 0, data7_io_num = 0;
    int max_transfer_sz = 0;
};
struct spi_device_interface_config_t {
    int mode = 0, clock_speed_hz = 0, spics_io_num = 0, queue_size = 0, input_delay_ns = 0;
    int flags = 0;
    void (*pre_cb)(spi_transaction_t *) = nullptr;
    void (*post_cb)(spi_transaction_t *) = nullptr;
};
struct FakeDevice { spi_device_interface_config_t config; };
using spi_device_handle_t = FakeDevice *;
esp_err_t spi_bus_initialize(int, const spi_bus_config_t *, int);
esp_err_t spi_bus_add_device(int, const spi_device_interface_config_t *, spi_device_handle_t *);
esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t *);
esp_err_t spi_bus_remove_device(spi_device_handle_t);
esp_err_t spi_bus_free(int);
