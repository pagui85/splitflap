// SPDX-License-Identifier: Apache-2.0
#include "bringup_display.hpp"
#include "dashboard.hpp"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include "driver/i2c_master.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_lcd_dsi.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "sdkconfig.h"

extern "C" esp_lcd_dpi_panel_config_t waveshare_7e_dpi_config(void);

namespace bringup::display {
namespace {
constexpr char kTag[] = "waveshare_7e";
static_assert(LV_DRAW_BUF_STRIDE_ALIGN == 1, "The RGB565 flush requires packed strips.");
constexpr int kWidth = 1280;
constexpr int kHeight = 800;
constexpr int kStripLines = 20;
constexpr size_t kFrameBytes = kWidth * kHeight * sizeof(uint16_t);
constexpr size_t kStripBytes = kWidth * kStripLines * sizeof(uint16_t);
constexpr int kI2cTimeout = 30;
constexpr uint8_t kBridgeAddress = 0x45;
constexpr bool kRotate = CONFIG_BRINGUP_WAVESHARE_ROTATION == 180;

enum class State { Off, Starting, Running, Failed };
std::atomic<State> state{State::Off};
std::atomic<const char *> stage{"manual_start_required"};
std::atomic<const char *> touch_status{"off"};
std::atomic<esp_err_t> last_error{ESP_OK};
std::atomic<uint32_t> frames_x10{0};
std::atomic<uint32_t> cpu_percent{0};
std::atomic<int> touch_address{0};
std::atomic<uint32_t> flush_errors{0};
std::atomic<bool> reported_flush_failure{false};
uint32_t frame_count; // ISR writes and the UI task samples.
i2c_master_bus_handle_t i2c{};
i2c_master_dev_handle_t bridge{};
esp_ldo_channel_handle_t ldo{};
esp_lcd_dsi_bus_handle_t dsi{};
esp_lcd_panel_io_handle_t dbi{};
esp_lcd_panel_handle_t panel{};
esp_lcd_panel_io_handle_t touch_io{};
esp_lcd_touch_handle_t touch{};
uint16_t native_x = kWidth;
uint16_t native_y = kHeight;
uint16_t *strip{};

const char *state_name()
{
    switch (state.load()) {
    case State::Off: return "off";
    case State::Starting: return "starting";
    case State::Running: return "streaming_unverified";
    case State::Failed: return "failed";
    }
    return "unknown";
}

void delay_ms(unsigned duration)
{
    vTaskDelay(pdMS_TO_TICKS(duration) + 1);
}

esp_err_t bridge_write(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(bridge, bytes, sizeof(bytes), kI2cTimeout);
}

bool IRAM_ATTR frame_complete(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *)
{
    __atomic_fetch_add(&frame_count, 1, __ATOMIC_RELAXED);
    return false;
}

esp_err_t setup_panel()
{
    stage = "psram";
    if (!esp_psram_is_initialized() || heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < kFrameBytes)
        return ESP_ERR_NO_MEM;
    strip = static_cast<uint16_t *>(heap_caps_malloc(kStripBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!strip) return ESP_ERR_NO_MEM;
    stage = "display_i2c";
    i2c_master_bus_config_t bus{};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_0;
    bus.scl_io_num = GPIO_NUM_1;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    // Use only the verified external pull-ups on the J6 system I2C bus.
    auto err = i2c_new_master_bus(&bus, &i2c);
    if (err != ESP_OK) return err;
    err = i2c_master_probe(i2c, kBridgeAddress, kI2cTimeout);
    if (err != ESP_OK) return err;
    i2c_device_config_t device{};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = kBridgeAddress;
    device.scl_speed_hz = 100000;
    err = i2c_master_bus_add_device(i2c, &device, &bridge);
    if (err != ESP_OK) return err;

    stage = "dphy";
    esp_ldo_channel_config_t rail{};
    rail.chan_id = 3;
    rail.voltage_mv = 2500;
    err = esp_ldo_acquire_channel(&rail, &ldo);
    if (err != ESP_OK) return err;
    esp_lcd_dsi_bus_config_t host{};
    host.bus_id = 0;
    host.num_data_lanes = 2;
    host.lane_bit_rate_mbps = 1250;
    err = esp_lcd_new_dsi_bus(&host, &dsi);
    if (err != ESP_OK) return err;
    const esp_lcd_dbi_io_config_t commands{0, 8, 8};
    err = esp_lcd_new_panel_io_dbi(dsi, &commands, &dbi);
    if (err != ESP_OK) return err;

    stage = "panel_bridge";
    // Waveshare esp_lcd_dsi 1.0.8 sequence, on the U213's display bus.
    // Keep the backlight off until the first LVGL frame is written.
    constexpr uint8_t sequence[][2] = {{0xc0, 1}, {0xc2, 1}, {0xac, 1},
                                       {0xab, 255}, {0xaa, 1}, {0xad, 1}};
    for (const auto &command : sequence) {
        err = bridge_write(command[0], command[1]);
        if (err != ESP_OK) return err;
    }
    delay_ms(1000);

    stage = "dpi_framebuffer";
    // Use the exact 7E timings from the pinned Waveshare component. Bypass
    // its constructor, which configures GPIO7/8 and owns the wrong I2C bus.
    const auto dpi = waveshare_7e_dpi_config();
    err = esp_lcd_new_panel_dpi(dsi, &dpi, &panel);
    if (err != ESP_OK) return err;
    void *frame = nullptr;
    err = esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &frame);
    if (err != ESP_OK) return err;
    if (!esp_ptr_external_ram(frame)) return ESP_ERR_INVALID_STATE;

    stage = "panel_commands";
    err = esp_lcd_panel_io_tx_param(dbi, LCD_CMD_SWRESET, nullptr, 0);
    if (err != ESP_OK) return err;
    delay_ms(120);
    const uint8_t zero = 0;
    err = esp_lcd_panel_io_tx_param(dbi, LCD_CMD_MADCTL, &zero, 1);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_io_tx_param(dbi, LCD_CMD_SLPOUT, &zero, 1);
    if (err != ESP_OK) return err;
    delay_ms(120);
    err = esp_lcd_panel_io_tx_param(dbi, LCD_CMD_DISPON, &zero, 1);
    if (err != ESP_OK) return err;
    delay_ms(20);
    esp_lcd_dpi_panel_event_callbacks_t callbacks{};
    callbacks.on_frame_buf_complete = frame_complete;
    err = esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, nullptr);
    if (err != ESP_OK) return err;
    return esp_lcd_panel_init(panel);
}

esp_err_t setup_touch()
{
    // Confirm the controller and read the configured coordinate range
    // before installing the touch driver. No address straps are driven.
    for (uint8_t address : {uint8_t{0x14}, uint8_t{0x5d}}) {
        if (i2c_master_probe(i2c, address, kI2cTimeout) != ESP_OK) continue;
        i2c_device_config_t probe{};
        probe.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        probe.device_address = address;
        probe.scl_speed_hz = 100000;
        i2c_master_dev_handle_t dev{};
        auto result = i2c_master_bus_add_device(i2c, &probe, &dev);
        if (result != ESP_OK) return result;
        const uint8_t product_reg[] = {0x81, 0x40};
        const uint8_t config_reg[] = {0x80, 0x48};
        uint8_t id[4]{};
        uint8_t resolution[4]{};
        result = i2c_master_transmit_receive(dev, product_reg, sizeof(product_reg), id, sizeof(id), kI2cTimeout);
        if (result == ESP_OK && std::memcmp(id, "911", 3) == 0)
            result = i2c_master_transmit_receive(dev, config_reg, sizeof(config_reg), resolution,
                                                 sizeof(resolution), kI2cTimeout);
        else
            result = ESP_ERR_NOT_SUPPORTED;
        i2c_master_bus_rm_device(dev);
        if (result != ESP_OK) continue;
        native_x = resolution[0] | (uint16_t(resolution[1]) << 8);
        native_y = resolution[2] | (uint16_t(resolution[3]) << 8);
        if (native_x < 2 || native_y < 2) continue;

        esp_lcd_panel_io_i2c_config_t io{};
        io.dev_addr = address;
        io.control_phase_bytes = 1;
        io.lcd_cmd_bits = 16;
        io.flags.disable_control_phase = true;
        io.transaction_timeout_ms = kI2cTimeout;
        io.scl_speed_hz = 100000;
        result = esp_lcd_new_panel_io_i2c(i2c, &io, &touch_io);
        if (result != ESP_OK) return result;
        esp_lcd_touch_config_t config{};
        config.x_max = native_x;
        config.y_max = native_y;
        config.rst_gpio_num = GPIO_NUM_NC;
        config.int_gpio_num = GPIO_NUM_NC;
        result = esp_lcd_touch_new_i2c_gt911(touch_io, &config, &touch);
        if (result != ESP_OK) {
            esp_lcd_panel_io_del(touch_io);
            touch_io = nullptr;
            return result;
        }
        touch_address = address;
        ESP_LOGI(kTag, "GT911 at 0x%02x, raw extent %u x %u, polling", address, native_x, native_y);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

void touch_read(lv_indev_t *, lv_indev_data_t *data)
{
    esp_lcd_touch_point_data_t point{};
    uint8_t count = 0;
    const auto result = esp_lcd_touch_read_data(touch);
    if (result == ESP_OK) esp_lcd_touch_get_data(touch, &point, &count, 1);
    touch_status = result == ESP_OK ? "gt911_polling" : "read_error";
    data->state = count ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (!count) {
        update_marker(0, 0, 0, 0, false);
        return;
    }
    int x = point.x;
    int y = point.y;
    int xmax = native_x;
    int ymax = native_y;
#if CONFIG_BRINGUP_WAVESHARE_TOUCH_SWAP_XY
    std::swap(x, y);
    std::swap(xmax, ymax);
#endif
    x = std::clamp(x, 0, xmax - 1) * (kWidth - 1) / (xmax - 1);
    y = std::clamp(y, 0, ymax - 1) * (kHeight - 1) / (ymax - 1);
#if CONFIG_BRINGUP_WAVESHARE_TOUCH_MIRROR_X
    x = kWidth - 1 - x;
#endif
#if CONFIG_BRINGUP_WAVESHARE_TOUCH_MIRROR_Y
    y = kHeight - 1 - y;
#endif
    if (kRotate) {
        x = kWidth - 1 - x;
        y = kHeight - 1 - y;
    }
    data->point.x = x;
    data->point.y = y;
    update_marker(point.x, point.y, x, y, true);
}

uint32_t ticks() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2 + 1;
    int y2 = area->y2 + 1;
    if (kRotate) {
        auto *start = reinterpret_cast<uint16_t *>(pixels);
        std::reverse(start, start + (x2 - x1) * (y2 - y1));
        const int left = kWidth - x2;
        const int top = kHeight - y2;
        x2 = kWidth - x1;
        y2 = kHeight - y1;
        x1 = left;
        y1 = top;
    }
    // With DMA2D disabled IDF copies synchronously and writes back the
    // destination cache before the DSI scanout DMA reads it.
    const auto result = esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, pixels);
    if (result != ESP_OK) {
        ++flush_errors;
        if (!reported_flush_failure.exchange(true)) ESP_LOGE(kTag, "Flush: %s", esp_err_to_name(result));
    }
    lv_display_flush_ready(display);
}

void release_hardware()
{
    if (touch) esp_lcd_touch_del(touch);
    if (touch_io) esp_lcd_panel_io_del(touch_io);
    if (panel) esp_lcd_panel_del(panel);
    if (dbi) esp_lcd_panel_io_del(dbi);
    if (dsi) esp_lcd_del_dsi_bus(dsi);
    if (ldo) esp_ldo_release_channel(ldo);
    if (bridge) i2c_master_bus_rm_device(bridge);
    if (i2c) i2c_del_master_bus(i2c);
    heap_caps_free(strip);
}

void task(void *)
{
    auto result = setup_panel();
    lv_display_t *display = nullptr;
    if (result == ESP_OK) {
        stage = "lvgl";
        lv_init();
        lv_tick_set_cb(ticks);
        display = lv_display_create(kWidth, kHeight);
        if (!display) result = ESP_ERR_NO_MEM;
    }
    if (result != ESP_OK) {
        last_error = result;
        ESP_LOGE(kTag, "%s: %s. Headless services remain available.", stage.load(), esp_err_to_name(result));
        release_hardware();
        state = State::Failed;
        vTaskDelete(nullptr);
        return;
    }

    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, strip, nullptr, kStripBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    const auto touch_result = setup_touch();
    touch_status = touch_result == ESP_OK ? "gt911_polling" : "unavailable";
    if (touch_result == ESP_OK) {
        lv_indev_t *input = lv_indev_create();
        if (input) {
            lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(input, display);
            lv_indev_set_read_cb(input, touch_read);
        } else {
            touch_status = "input_allocation_failed";
        }
    } else {
        ESP_LOGW(kTag, "Touch: %s", esp_err_to_name(touch_result));
    }
    create_dashboard(display, std::strcmp(touch_status.load(), "gt911_polling") == 0);
    lv_refr_now(display);
    const auto light = brightness(40);
    if (light != ESP_OK) ESP_LOGW(kTag, "Backlight: %s", esp_err_to_name(light));
    stage = "scanout";
    state = State::Running;
    ESP_LOGI(kTag, "RGB565 1280x800, %u-byte PSRAM framebuffer, %u-byte internal strip. Verify panel visually.",
             unsigned(kFrameBytes), unsigned(kStripBytes));

    uint32_t sample_time = ticks();
    uint32_t last_frame = __atomic_load_n(&frame_count, __ATOMIC_RELAXED);
    while (true) {
        const auto next = lv_timer_handler();
        const uint32_t now = ticks();
        const uint32_t elapsed = now - sample_time;
        if (elapsed >= 1000) {
            const uint32_t frames = __atomic_load_n(&frame_count, __ATOMIC_RELAXED);
            frames_x10 = (frames - last_frame) * 10000u / elapsed;
            last_frame = frames;
            sample_time = now;
            cpu_percent = 100 - std::min<uint32_t>(100,
                (ulTaskGetIdleRunTimePercentForCore(0) + ulTaskGetIdleRunTimePercentForCore(1)) / 2);
        }
        delay_ms(std::clamp<uint32_t>(next, 5, 25));
    }
}
}

const char *touch_state() { return touch_status.load(); }

esp_err_t brightness(unsigned percent)
{
    return bridge_write(0xab, 255 - 255 * std::min(percent, 100u) / 100);
}

int health_fields(char *output, std::size_t capacity)
{
    return std::snprintf(output, capacity,
        ",\"display_7e\":\"%s\",\"display_stage\":\"%s\",\"display_error\":\"%s\","
        "\"touch\":\"%s\",\"touch_address\":%d,\"framebuffer_bytes\":%u,"
        "\"display_frames_x10\":%" PRIu32 ",\"display_flush_errors\":%" PRIu32 ",\"cpu_busy_percent\":%" PRIu32,
        state_name(), stage.load(), esp_err_to_name(last_error.load()), touch_status.load(), touch_address.load(),
        state == State::Running ? unsigned(kFrameBytes) : 0, frames_x10.load(), flush_errors.load(), cpu_percent.load());
}

int console(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "start") == 0) {
        esp_chip_info_t chip{};
        esp_chip_info(&chip);
        if (chip.revision < 300) {
            std::puts("Waveshare 7E BSP supports P4 v3.x. This is an experimental early-silicon bench test.");
            std::puts("After checking the adapter and panel power, use: display start experimental");
            return 1;
        }
    }
    if ((argc == 2 || (argc == 3 && std::strcmp(argv[2], "experimental") == 0)) &&
        std::strcmp(argv[1], "start") == 0) {
        State expected = State::Off;
        if (!state.compare_exchange_strong(expected, State::Starting)) {
            std::puts("Display already started or attempted. Reboot for another bench attempt.");
            return 1;
        }
        if (xTaskCreatePinnedToCore(task, "waveshare_7e", 12288, nullptr, 3, nullptr, 1) != pdPASS) {
            state = State::Failed;
            stage = "task_allocation";
            last_error = ESP_ERR_NO_MEM;
            return 1;
        }
        return 0;
    }
    if (argc != 1) {
        std::puts("Use display or display start experimental.");
        return 1;
    }
    char fields[340];
    health_fields(fields, sizeof(fields));
    std::printf("{%s}\ninternal_free=%zu external_free=%zu\n", fields + 1,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}
}
