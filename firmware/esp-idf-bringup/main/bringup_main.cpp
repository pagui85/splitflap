// SPDX-License-Identifier: Apache-2.0
#include "diagnostic_core.hpp"
#include "build_metadata.hpp"
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {
constexpr const char *kTag = "s3_bringup";
bringup::ClockState clock_state;
constexpr int kPins[] = {CONFIG_BRINGUP_PIN_MOSI, CONFIG_BRINGUP_PIN_MISO,
                        CONFIG_BRINGUP_PIN_CLOCK, CONFIG_BRINGUP_PIN_LATCH, CONFIG_BRINGUP_PIN_OE};
constexpr const char *kPinNames[] = {"MOSI", "MISO", "CLOCK", "LATCH", "OE_n"};
constexpr int kCandidatePins[] = {11, 13, 12, 10, -1};

bool set_clock(std::int64_t epoch)
{
    timeval value{};
    value.tv_sec = static_cast<time_t>(epoch);
    if (settimeofday(&value, nullptr) == 0) return true;
    std::printf("clock_set_error=%s\n", std::strerror(errno));
    return false;
}
void print_time()
{
    const time_t now = std::time(nullptr);
    tm utc{};
    char iso[32] = "unrepresentable";
    if (gmtime_r(&now, &utc)) std::strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &utc);
    std::printf("utc=%s epoch=%" PRId64 "\n", iso, static_cast<std::int64_t>(now));
    std::printf("time_source=%s provisioned_this_boot=%s\n", clock_state.source_name(),
                clock_state.provisioned() ? "yes" : "no");
    std::printf("time_trust=development_only EAP_TLS_authorized=%s persistent=no\n",
                clock_state.eap_authorized() ? "yes" : "no");
}
void print_pins()
{
    std::puts("standalone=yes Chainlink_GPIO_activation=disabled (no HAL linked)");
    for (std::size_t i = 0; i < 5; ++i) {
        const bool conflict = bringup::pin_conflict(kPins, 5, i);
        std::printf("%s reported_assignment=%d candidate_only=%d status=%s applied=no\n",
                    kPinNames[i], kPins[i], kCandidatePins[i],
                    kPins[i] == -1 ? "unassigned" : conflict ? "CONFLICT_reserved_or_duplicate" : "policy_candidate_unverified");
    }
    std::puts("UART0_console=TX43/RX44 115200; native USB19/20 not reassigned");
    std::puts("Protected: straps 0/3/45/46; memory 26..37; USB19/20; UART43/44; JTAG39..42; LEDs38/48");
    std::puts("No electrical level guarantee: ROM/SDK/board circuitry can affect pins. Attach no harness.");
}
void print_spi()
{
    std::puts("Chainlink_SPI2_driver_linked=no initialized=no transfers=0 activation_command=none");
    std::puts("Future plan only: TX mode3, RX mode2, MSB first, 4MHz, RX delay30ns; NOT applied");
    std::puts("Latch/clock/OE are not configured or pulsed by this diagnostic application.");
    std::puts("Normal boot flash-memory traffic is separate from Chainlink SPI2.");
}
const char *reset_name(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_EXT: return "external_pin";
        case ESP_RST_SW: return "software_reboot";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt_watchdog";
        case ESP_RST_TASK_WDT: return "task_watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        case ESP_RST_USB: return "usb";
        case ESP_RST_JTAG: return "jtag";
        case ESP_RST_EFUSE: return "efuse_error";
        case ESP_RST_PWR_GLITCH: return "power_glitch";
        case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
        default: return "unknown";
    }
}
void print_psram()
{
    uint8_t low = 0, high = 0;
    const esp_err_t low_result = esp_efuse_read_field_blob(ESP_EFUSE_PSRAM_CAP, &low, 2);
    // Espressif esptool v4.13.0 combines BLOCK1 bits131..132 with bit179.
    // IDF v5.5.6's named PSRAM_CAP descriptor includes only the low two bits.
    const esp_err_t high_result = esp_efuse_read_block(EFUSE_BLK1, &high, 179, 1);
    if (low_result == ESP_OK && high_result == ESP_OK) {
        const unsigned code = (low & 3u) | ((high & 1u) << 2);
        const std::size_t hint = bringup::package_psram_bytes(code);
        std::printf("psram_package_capacity_code=%u factory_hint_bytes=%zu\n", code, hint);
        std::printf("psram_presence=%s\n", hint ? "package_indicates_present_not_functionally_tested" : "unknown_not_probed");
    } else {
        std::printf("psram_package_read_status=%s/%s presence=unknown\n",
                    esp_err_to_name(low_result), esp_err_to_name(high_result));
    }
    std::printf("psram_initialization=not_attempted_CONFIG_SPIRAM_off initialized_bytes=0 heap_total_bytes=%zu heap_free_bytes=%zu\n",
                heap_caps_get_total_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    std::puts("PSRAM hint is not initialized/usable RAM. Code0/unknown does not prove no separately fitted PSRAM.");
}
void print_info()
{
    std::puts("--- standalone ESP32-S3 bring-up info ---");
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    std::printf("chip=%s model_id=%d silicon_revision=%u.%u raw_revision=%u cores=%u\n",
                chip.model == CHIP_ESP32S3 ? "ESP32-S3" : "UNEXPECTED", static_cast<int>(chip.model),
                chip.revision / 100, chip.revision % 100, chip.revision, chip.cores);
    std::printf("idf=%s firmware=splitflap_s3_bringup/0.3.0\n", esp_get_idf_version());
    std::printf("build_utc=%s epoch=%" PRId64 " source=%s (approximate, untrusted)\n",
                bringup::kBuildUtc, bringup::kBuildEpoch, bringup::kBuildSource);
    const auto reason = esp_reset_reason();
    std::printf("reset_reason=%s code=%d\n", reset_name(reason), static_cast<int>(reason));
    uint32_t physical = 0, image = 0, id = 0;
    const esp_err_t physical_result = esp_flash_get_physical_size(nullptr, &physical);
    const esp_err_t image_result = esp_flash_get_size(nullptr, &image);
    const esp_err_t id_result = esp_flash_read_id(nullptr, &id);
    std::printf("flash_physical_bytes=%" PRIu32 " status=%s (JEDEC-based detection)\n", physical, esp_err_to_name(physical_result));
    std::printf("flash_image_access_limit_bytes=%" PRIu32 " status=%s; configured_header=%s DIO/40MHz\n",
                image, esp_err_to_name(image_result), CONFIG_ESPTOOLPY_FLASHSIZE);
    std::printf("flash_jedec_id=0x%06" PRIx32 " status=%s\n", id, esp_err_to_name(id_result));
    print_psram();
    constexpr uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    std::printf("heap_free_bytes=%" PRIu32 " heap_minimum_free_bytes=%" PRIu32 "\n",
                esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    std::printf("internal_8bit_total_bytes=%zu free_bytes=%zu largest_block_bytes=%zu minimum_free_bytes=%zu\n",
                heap_caps_get_total_size(internal), heap_caps_get_free_size(internal),
                heap_caps_get_largest_free_block(internal), heap_caps_get_minimum_free_size(internal));
    print_time();
    print_pins();
    print_spi();
    std::puts("--- end info; no Buddy, harness, motors, Wi-Fi or credentials required ---");
}
int command_handler(int argc, char **argv)
{
    const auto request = bringup::parse_command(argc, argv);
    switch (request.command) {
        case bringup::Command::Info: print_info(); return 0;
        case bringup::Command::Pins: print_pins(); return 0;
        case bringup::Command::Spi: print_spi(); return 0;
        case bringup::Command::Time: print_time(); return 0;
        case bringup::Command::SetTime:
            if (!clock_state.provision(request.epoch, set_clock)) return 1;
            print_time(); return 0;
        case bringup::Command::Reboot:
            std::puts("Restarting; manual provisioning status is not persisted.");
            std::fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart(); return 0;
        default:
            std::puts("Invalid arguments. Use help; time set requires YYYY-MM-DDTHH:MM:SSZ (1970..2099, no leap seconds).");
            return 1;
    }
}
esp_err_t register_commands()
{
    esp_err_t result = esp_console_register_help_command();
    if (result != ESP_OK) return result;
    struct Entry { const char *name; const char *help; };
    constexpr Entry entries[] = {
        {"info", "Read chip, physical flash, PSRAM hints, heap, build/reset/time and safety report"},
        {"pins", "Report assignments/conflicts only; never activate GPIO"},
        {"spi", "Report inactive Chainlink SPI2 status; never transmit"},
        {"time", "Show UTC/provisioning state, or: time set YYYY-MM-DDTHH:MM:SSZ (development only)"},
        {"reboot", "Restart; manual time provenance is forgotten, no persistent writes"},
    };
    for (const auto &entry : entries) {
        esp_console_cmd_t command{};
        command.command = entry.name;
        command.help = entry.help;
        command.func = command_handler;
        result = esp_console_cmd_register(&command);
        if (result != ESP_OK) return result;
    }
    return ESP_OK;
}
} // namespace

extern "C" void app_main()
{
    // No NVS, filesystem, GPIO/SPI2 HAL, Wi-Fi, PSRAM probe or event loop startup.
    if (!clock_state.bootstrap(bringup::kBuildEpoch, static_cast<std::int64_t>(std::time(nullptr)), set_clock))
        ESP_LOGE(kTag, "Clock bootstrap failed; time remains unprovisioned");
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "s3-bringup> ";
    config.max_cmdline_length = 96;
    config.max_cmdline_args = 4;
    config.max_history_len = 8;
    config.history_save_path = nullptr;
    config.task_stack_size = 6144;
    esp_console_dev_uart_config_t uart = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_repl_t *repl = nullptr;
    esp_err_t result = esp_console_new_repl_uart(&uart, &config, &repl);
    if (result != ESP_OK) { ESP_LOGE(kTag, "UART0 REPL setup failed: %s", esp_err_to_name(result)); return; }
    result = register_commands();
    if (result == ESP_OK) {
        print_info();
        std::puts("Type help or info. Standalone only: keep every external header disconnected.");
        result = esp_console_start_repl(repl);
    }
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Console startup failed: %s", esp_err_to_name(result));
        repl->del(repl);
    }
}
