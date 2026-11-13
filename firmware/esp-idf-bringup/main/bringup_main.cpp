// SPDX-License-Identifier: Apache-2.0
#include "diagnostic_core.hpp"
#include "build_metadata.hpp"
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_clk_tree.h"
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
#if CONFIG_IDF_TARGET_ESP32P4
#include "bringup_ethernet.hpp"
#if CONFIG_BRINGUP_WAVESHARE_7E
#include "bringup_display.hpp"
#endif
#if CONFIG_BRINGUP_U213_OTA
#include "bringup_ota.hpp"
#endif
#include "chainlink_pin_policy.hpp"
#include "driver/gpio.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif
#endif

namespace {
#if CONFIG_IDF_TARGET_ESP32P4
constexpr char kTag[] = "p4_bringup";
constexpr char kPrompt[] = "p4-bringup> ";
constexpr char kChipName[] = "ESP32-P4";
constexpr auto kChip = CHIP_ESP32P4;
#else
constexpr char kTag[] = "s3_bringup";
constexpr char kPrompt[] = "s3-bringup> ";
constexpr char kChipName[] = "ESP32-S3";
constexpr auto kChip = CHIP_ESP32S3;
#endif
bringup::ClockState clock_state;
constexpr int kPins[] = {CONFIG_BRINGUP_PIN_MOSI, CONFIG_BRINGUP_PIN_MISO,
                        CONFIG_BRINGUP_PIN_CLOCK, CONFIG_BRINGUP_PIN_LATCH, CONFIG_BRINGUP_PIN_OE};
constexpr const char *kPinNames[] = {"MOSI", "MISO", "CLOCK", "LATCH", "OE_n"};
#if CONFIG_IDF_TARGET_ESP32S3
constexpr int kCandidatePins[] = {11, 13, 12, 10, -1};
#else
const char *p4_pin_status(std::size_t index)
{
    const int pin = kPins[index];
    if (pin == -1) return "unassigned";
    if (!GPIO_IS_VALID_GPIO(pin) || (index != 1 && !GPIO_IS_VALID_OUTPUT_GPIO(pin)))
        return "CONFLICT_invalid_gpio";
#if CONFIG_BRINGUP_WAVESHARE_7E
    if (pin == 0 || pin == 1) return "CONFLICT_display_system_i2c";
#endif
    if (bringup::ethernet::uses_pin(pin)) return "CONFLICT_configured_ethernet";
    if (splitflap::chainlink::p4_reserved_pin(pin)) return "CONFLICT_usb_strap_or_uart";
    for (std::size_t other = 0; other < sizeof(kPins) / sizeof(kPins[0]); ++other) {
        if (other != index && kPins[other] == pin) return "CONFLICT_duplicate";
    }
    return "unverified_board_and_memory_routing";
}
#endif

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
#if CONFIG_IDF_TARGET_ESP32P4
    for (std::size_t i = 0; i < sizeof(kPins) / sizeof(kPins[0]); ++i) {
        std::printf("%s reported_assignment=%d status=%s applied=no\n",
                    kPinNames[i], kPins[i], p4_pin_status(i));
    }
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    std::puts("console=USB_Serial_JTAG board USB routing applies");
#else
    std::puts("console=UART0 TX37/RX38 115200");
#endif
#if CONFIG_BRINGUP_WAVESHARE_7E
    std::puts("J6 display I2C: SDA=GPIO0 SCL=GPIO1, DSI=fixed PHY, reset/IRQ/boost=unused");
    std::puts("Underside GPIO8..13 remain untouched by display and Chainlink.");
#endif
    std::puts("Protected: USB24..27 and straps34..38. Board-specific memory/peripheral routing still requires review.");
#else
    for (std::size_t i = 0; i < 5; ++i) {
        const bool conflict = bringup::pin_conflict(kPins, 5, i);
        std::printf("%s reported_assignment=%d candidate_only=%d status=%s applied=no\n",
                    kPinNames[i], kPins[i], kCandidatePins[i],
                    kPins[i] == -1 ? "unassigned" : conflict ? "CONFLICT_reserved_or_duplicate" : "policy_candidate_unverified");
    }
    std::puts("UART0_console=TX43/RX44 115200; native USB19/20 not reassigned");
    std::puts("Protected: straps 0/3/45/46; memory 26..37; USB19/20; UART43/44; JTAG39..42; LEDs38/48");
#endif
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
#if CONFIG_IDF_TARGET_ESP32P4
#if CONFIG_SPIRAM
    const bool initialized = esp_psram_is_initialized();
    const std::size_t detected = esp_psram_get_size();
    const char *state = initialized ? "initialized" : "unavailable_check_boot_log";
#else
    constexpr std::size_t detected = 0;
    constexpr const char *state = "not_attempted_CONFIG_SPIRAM_off";
#endif
    std::printf("psram_detected_bytes=%zu initialization=%s heap_total_bytes=%zu heap_free_bytes=%zu\n",
                detected, state, heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#else
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
#endif
}
void print_info()
{
    std::printf("--- standalone %s bring-up info ---\n", kChipName);
#if CONFIG_BRINGUP_BOARD_U213
    std::puts("board_profile=m5stack_u213_early");
#endif
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    std::printf("chip=%s model_id=%d silicon_revision=%u.%u raw_revision=%u cores=%u\n",
                chip.model == kChip ? kChipName : "UNEXPECTED", static_cast<int>(chip.model),
                chip.revision / 100, chip.revision % 100, chip.revision, chip.cores);
    uint32_t cpu_hz = 0;
    const auto cpu_result = esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &cpu_hz);
    std::printf("cpu_hz=%" PRIu32 " status=%s\n", cpu_hz, esp_err_to_name(cpu_result));
    const auto &app = *esp_app_get_description();
    std::printf("idf=%s firmware=%s/%s\n", esp_get_idf_version(), app.project_name, app.version);
    std::printf("build_utc=%s epoch=%" PRId64 " source=%s (approximate, untrusted)\n",
                bringup::kBuildUtc, bringup::kBuildEpoch, bringup::kBuildSource);
    const auto reason = esp_reset_reason();
    std::printf("reset_reason=%s code=%d\n", reset_name(reason), static_cast<int>(reason));
    uint32_t physical = 0, image = 0, id = 0;
    const esp_err_t physical_result = esp_flash_get_physical_size(nullptr, &physical);
    const esp_err_t image_result = esp_flash_get_size(nullptr, &image);
    const esp_err_t id_result = esp_flash_read_id(nullptr, &id);
    std::printf("flash_physical_bytes=%" PRIu32 " status=%s (JEDEC-based detection)\n", physical, esp_err_to_name(physical_result));
    std::printf("flash_image_access_limit_bytes=%" PRIu32 " status=%s; configured_header=%s %s/%s\n",
                image, esp_err_to_name(image_result), CONFIG_ESPTOOLPY_FLASHSIZE,
                CONFIG_ESPTOOLPY_FLASHMODE, CONFIG_ESPTOOLPY_FLASHFREQ);
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
#if CONFIG_IDF_TARGET_ESP32P4
    bringup::ethernet::print_status();
#endif
    std::puts("--- end info; no Buddy, harness, motors, Wi-Fi or credentials required ---");
}
int command_handler(int argc, char **argv)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (std::strcmp(argv[0], "net") == 0) {
        if (argc != 1) {
            std::puts("Usage: net");
            return 1;
        }
        bringup::ethernet::print_status();
        return 0;
    }
#endif
#if CONFIG_BRINGUP_U213_OTA
    if (argc > 0 && (std::strcmp(argv[0], "ota") == 0 || std::strcmp(argv[0], "config") == 0))
        return bringup::ota::console_command(argc, argv);
#endif
#if CONFIG_BRINGUP_WAVESHARE_7E
    if (argc > 0 && std::strcmp(argv[0], "display") == 0) return bringup::display::console(argc, argv);
#endif
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
#if CONFIG_IDF_TARGET_ESP32P4
        {"net", "Show Ethernet configuration, link and DHCP status"},
#endif
#if CONFIG_BRINGUP_U213_OTA
        {"ota", "Show OTA state, or ota provision over USB to create credentials"},
        {"config", "Show persistent settings, or config probe <uint32> for retention testing"},
#endif
#if CONFIG_BRINGUP_WAVESHARE_7E
        {"display", "Display status, or display start experimental for the early-P4 bench test"},
#endif
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
#if CONFIG_BRINGUP_U213_OTA
    bringup::ota::initialize();
#endif
    // Chainlink and Wi-Fi stay inactive in every diagnostic profile.
    if (!clock_state.bootstrap(bringup::kBuildEpoch, static_cast<std::int64_t>(std::time(nullptr)), set_clock))
        ESP_LOGE(kTag, "Clock bootstrap failed; time remains unprovisioned");
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = kPrompt;
    config.max_cmdline_length = 96;
    config.max_cmdline_args = 4;
    config.max_history_len = 8;
    config.history_save_path = nullptr;
#if CONFIG_BRINGUP_U213_OTA
    config.task_stack_size = 12288; // Certificate and key generation runs on this task.
#else
    config.task_stack_size = 6144;
#endif
    esp_console_repl_t *repl = nullptr;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t result = esp_console_new_repl_usb_serial_jtag(&usb, &config, &repl);
#else
    esp_console_dev_uart_config_t uart = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t result = esp_console_new_repl_uart(&uart, &config, &repl);
#endif
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Console REPL setup failed: %s", esp_err_to_name(result));
#if CONFIG_BRINGUP_U213_OTA
        bringup::ota::verify_boot(false);
#endif
        return;
    }
    result = register_commands();
    if (result == ESP_OK) {
#if CONFIG_IDF_TARGET_ESP32P4
        bringup::ethernet::start();
#endif
        print_info();
        std::puts("Type help or info. Standalone only: keep every external header disconnected.");
        result = esp_console_start_repl(repl);
    }
#if CONFIG_BRINGUP_U213_OTA
    bringup::ota::verify_boot(result == ESP_OK && bringup::ethernet::services_ok());
#endif
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Console startup failed: %s", esp_err_to_name(result));
        repl->del(repl);
    }
}
