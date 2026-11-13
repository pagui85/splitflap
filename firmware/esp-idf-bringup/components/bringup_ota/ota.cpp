// SPDX-License-Identifier: Apache-2.0
#include "bringup_ota.hpp"
#include "credentials.hpp"
#include "persistent.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "bootloader_random.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_chip_info.h"
#include "esp_https_server.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern "C" {
#include "mbedtls/constant_time.h"
}
#include "mbedtls/sha256.h"
#include "sdkconfig.h"

// Fixed descriptor immediately after the IDF app description. Identifies the
// U213 OTA layout and boot contract independently of the release version.
extern "C" const char splitflap_u213_ota_descriptor[]
    __attribute__((section(".rodata_custom_desc"))) = "splitflap-u213-ota:v1";

namespace bringup::ota {
namespace {
#if CONFIG_BRINGUP_OTA_TEST_FAIL
constexpr bool kFailTrial = true;
#else
constexpr bool kFailTrial = false;
#endif
constexpr char kTag[] = "bringup_ota";
constexpr size_t kDescriptorOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
constexpr size_t kPrefixSize = kDescriptorOffset + sizeof(esp_app_desc_t) + sizeof(splitflap_u213_ota_descriptor);
constexpr int64_t kUploadDeadlineUs = 180 * INT64_C(1000000);
bool config_ok = false;
bool credentials_ok = false;
bool layout_ok = false;
std::atomic<bool> pending{false};
std::atomic<bool> startup_checked{false};
bool provisioned_this_boot = false;
bool reboot_pending = false; // Only the HTTPS server task stages an update.
httpd_handle_t https_server = nullptr;
esp_timer_handle_t trial_timer = nullptr;

bool validate_layout()
{
    struct ExpectedPartition {
        const char *label;
        esp_partition_type_t type;
        esp_partition_subtype_t subtype;
        uint32_t address;
        uint32_t size;
    };
    constexpr ExpectedPartition expected[] = {
        {"otadata", ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, 0xe000, 0x2000},
        {"factory", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, 0x10000, 0x300000},
        {"ota_0", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, 0x310000, 0x400000},
        {"ota_1", ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x710000, 0x400000},
        {kConfigPartition, ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, 0xb10000, 0x10000},
        {kKeysPartition, ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, 0xb20000, 0x10000},
    };
    if (esp_ota_get_app_partition_count() != 2) return false;
    for (const auto &item : expected) {
        const auto *partition = esp_partition_find_first(item.type, item.subtype, item.label);
        if (!partition || partition->address != item.address || partition->size != item.size) return false;
    }
    return true;
}

const char *state_name()
{
    const auto *running = esp_ota_get_running_partition();
    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) return "factory";
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) return "unknown";
    switch (state) {
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    default: return "undefined";
    }
}

void record_result(UpdateRecord &record, Result result)
{
    record.result = result;
    const auto error = save_update(record);
    if (error != ESP_OK) ESP_LOGE(kTag, "Update journal: %s", esp_err_to_name(error));
}

void reconcile_update()
{
    UpdateRecord record{};
    if (read_update(record) != ESP_OK || record.result != Result::Staged) return;
    const auto *running = esp_ota_get_running_partition();
    if (running->subtype == record.slot) {
        if (std::strcmp(state_name(), "valid") == 0 &&
            std::memcmp(esp_app_get_description()->app_elf_sha256, record.build, sizeof(record.build)) == 0)
            record_result(record, Result::Confirmed);
        return;
    }
    const auto *attempted = esp_partition_find_first(ESP_PARTITION_TYPE_APP, static_cast<esp_partition_subtype_t>(record.slot), nullptr);
    esp_ota_img_states_t state;
    if (attempted && esp_ota_get_state_partition(attempted, &state) == ESP_OK &&
        (state == ESP_OTA_IMG_ABORTED || state == ESP_OTA_IMG_INVALID)) {
        // Ensure stale metadata for an earlier attempt on the same slot
        // does not describe this build.
        esp_app_desc_t app{};
        if (esp_ota_get_partition_description(attempted, &app) == ESP_OK &&
            std::memcmp(app.app_elf_sha256, record.build, sizeof(record.build)) == 0) {
            record_result(record, Result::RolledBack);
            return;
        }
    }
    record_result(record, Result::Interrupted);
}

esp_err_t respond(httpd_req_t *request, const char *status, const char *message, bool close = true)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (close) httpd_resp_set_hdr(request, "Connection", "close");
    const auto result = httpd_resp_sendstr(request, message);
    return close ? ESP_FAIL : result;
}

bool receive(httpd_req_t *request, uint8_t *data, size_t size, int64_t deadline)
{
    while (size != 0) {
        if (esp_timer_get_time() >= deadline) return false;
        const int count = httpd_req_recv(request, reinterpret_cast<char *>(data), size);
        if (count <= 0) return false;
        data += count;
        size -= count;
    }
    return true;
}

bool inspect_prefix(const uint8_t *data, esp_app_desc_t &app)
{
    esp_image_header_t header{};
    std::memcpy(&header, data, sizeof(header));
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.segment_count == 0 ||
        header.segment_count > ESP_IMAGE_MAX_SEGMENTS || header.chip_id != ESP_CHIP_ID_ESP32P4 ||
        chip.revision < header.min_chip_rev_full || chip.revision > header.max_chip_rev_full ||
        header.spi_size != ESP_IMAGE_FLASH_SIZE_16MB || header.hash_appended != 1)
        return false;
    std::memcpy(&app, data + kDescriptorOffset, sizeof(app));
    return app.magic_word == ESP_APP_DESC_MAGIC_WORD &&
           std::memcmp(app.project_name, "splitflap_p4_bringup", sizeof("splitflap_p4_bringup")) == 0 &&
           std::memcmp(data + kDescriptorOffset + sizeof(app), splitflap_u213_ota_descriptor,
                       sizeof(splitflap_u213_ota_descriptor)) == 0;
}

void reboot_after_response(void *)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

bool authenticate(httpd_req_t *request)
{
    char authorization[72]{};
    return httpd_req_get_hdr_value_str(request, "Authorization", authorization, sizeof(authorization)) == ESP_OK &&
           std::strncmp(authorization, "Bearer ", 7) == 0 && authorized(authorization + 7);
}

esp_err_t upload(httpd_req_t *request)
{
    if (!authenticate(request))
        return respond(request, "401 Unauthorized", "{\"error\":\"authentication_required\"}");
    if (!config_ok || pending || reboot_pending)
        return respond(request, "409 Conflict", "{\"error\":\"ota_unavailable\"}");

    // Reject alternate body framing. The supported uploader sends one raw
    // app image with Content-Length and its exact SHA-256.
    char type[32]{};
    char sha[65]{};
    uint8_t expected[kDigestSize];
    if (httpd_req_get_hdr_value_len(request, "Transfer-Encoding") != 0 ||
        httpd_req_get_hdr_value_str(request, "Content-Type", type, sizeof(type)) != ESP_OK ||
        std::strcmp(type, "application/octet-stream") != 0 ||
        httpd_req_get_hdr_value_str(request, "X-Image-SHA256", sha, sizeof(sha)) != ESP_OK ||
        !hex_decode(sha, sizeof(expected), expected))
        return respond(request, "400 Bad Request", "{\"error\":\"invalid_headers\"}");

    const auto *slot = esp_ota_get_next_update_partition(nullptr);
    if (!slot || slot == esp_ota_get_running_partition())
        return respond(request, "409 Conflict", "{\"error\":\"no_inactive_slot\"}");
    if (request->content_len < kPrefixSize || request->content_len > slot->size)
        return respond(request, "413 Payload Too Large", "{\"error\":\"invalid_image_length\"}");

    UpdateRecord record{};
    record.slot = slot->subtype;
    record.result = Result::Interrupted;
    if (save_update(record) != ESP_OK)
        return respond(request, "500 Internal Server Error", "{\"error\":\"journal_unavailable\"}");

    const auto deadline = esp_timer_get_time() + kUploadDeadlineUs;
    // Keep the HTTP task stack independent of the transfer buffer size.
    static std::array<uint8_t, 4096> buffer;
    if (!receive(request, buffer.data(), kPrefixSize, deadline))
        return respond(request, "408 Request Timeout", "{\"error\":\"incomplete_upload\"}");
    esp_app_desc_t app{};
    if (!inspect_prefix(buffer.data(), app)) {
        record_result(record, Result::RejectedImage);
        return respond(request, "422 Unprocessable Content", "{\"error\":\"incompatible_image\"}");
    }
    std::memcpy(record.build, app.app_elf_sha256, sizeof(record.build));

    esp_ota_handle_t handle;
    auto error = esp_ota_begin(slot, request->content_len, &handle);
    if (error != ESP_OK) {
        record_result(record, Result::WriteError);
        return respond(request, "500 Internal Server Error", "{\"error\":\"ota_begin_failed\"}");
    }
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    int hash_error = mbedtls_sha256_starts(&hash, 0);
    bool complete = true;
    size_t written = 0;
    size_t count = kPrefixSize;
    while (hash_error == 0 && error == ESP_OK) {
        hash_error = mbedtls_sha256_update(&hash, buffer.data(), count);
        if (hash_error != 0) break;
        error = esp_ota_write(handle, buffer.data(), count);
        if (error != ESP_OK) break;
        written += count;
        if (written == request->content_len) break;
        const size_t left = request->content_len - written;
        count = left < buffer.size() ? left : buffer.size();
        complete = receive(request, buffer.data(), count, deadline);
        if (!complete) break;
    }
    uint8_t actual[kDigestSize]{};
    if (hash_error == 0) hash_error = mbedtls_sha256_finish(&hash, actual);
    mbedtls_sha256_free(&hash);

    if (error != ESP_OK || hash_error != 0 || !complete || mbedtls_ct_memcmp(expected, actual, sizeof(actual)) != 0) {
        esp_ota_abort(handle);
        if (!complete)
            return respond(request, "408 Request Timeout", "{\"error\":\"incomplete_upload\"}");
        if (error != ESP_OK || hash_error != 0) {
            record_result(record, Result::WriteError);
            return respond(request, "500 Internal Server Error", "{\"error\":\"ota_write_failed\"}");
        }
        record_result(record, Result::RejectedImage);
        return respond(request, "422 Unprocessable Content", "{\"error\":\"image_sha256_mismatch\"}");
    }

    // esp_ota_end validates the full image and closes the handle even on failure.
    error = esp_ota_end(handle);
    esp_image_metadata_t metadata{};
    const esp_partition_pos_t position{slot->address, slot->size};
    if (error != ESP_OK || esp_image_get_metadata(&position, &metadata) != ESP_OK ||
        metadata.image_len != request->content_len) {
        record_result(record, Result::RejectedImage);
        return respond(request, "422 Unprocessable Content", "{\"error\":\"invalid_esp_image\"}");
    }

    record.result = Result::Staged;
    if (save_update(record) != ESP_OK)
        return respond(request, "500 Internal Server Error", "{\"error\":\"journal_unavailable\"}");
    error = esp_ota_set_boot_partition(slot);
    if (error != ESP_OK) {
        record_result(record, Result::WriteError);
        return respond(request, "500 Internal Server Error", "{\"error\":\"boot_selection_failed\"}");
    }
    reboot_pending = true;
    char build[65];
    hex_encode(record.build, sizeof(record.build), build);
    char reply[160];
    std::snprintf(reply, sizeof(reply), "{\"status\":\"staged\",\"partition\":\"%s\",\"build_id\":\"%s\"}", slot->label, build);
    const auto result = respond(request, "202 Accepted", reply, false);
    if (xTaskCreate(reboot_after_response, "ota_reboot", 2048, nullptr, 5, nullptr) != pdPASS)
        reboot_after_response(nullptr);
    return result;
}

esp_err_t confirm(httpd_req_t *request)
{
    if (!authenticate(request))
        return respond(request, "401 Unauthorized", "{\"error\":\"authentication_required\"}");
    char expected[65]{};
    char running[65];
    hex_encode(esp_app_get_description()->app_elf_sha256, 32, running);
    if (request->content_len != 0 ||
        httpd_req_get_hdr_value_str(request, "X-Build-ID", expected, sizeof(expected)) != ESP_OK ||
        std::strcmp(expected, running) != 0)
        return respond(request, "400 Bad Request", "{\"error\":\"build_identifier_required\"}");
    if (!pending || !startup_checked)
        return respond(request, "409 Conflict", "{\"error\":\"trial_not_ready\"}");
    // Claim the trial before touching OTA metadata so the timer and the
    // confirmation handler cannot both act on it.
    if (!pending.exchange(false))
        return respond(request, "409 Conflict", "{\"error\":\"trial_not_ready\"}");
    const auto result = esp_ota_mark_app_valid_cancel_rollback();
    if (result != ESP_OK) {
        respond(request, "500 Internal Server Error", "{\"error\":\"confirmation_failed\"}");
        reboot_after_response(nullptr);
        return ESP_FAIL;
    }
    esp_timer_stop(trial_timer);
    reconcile_update();
    ESP_LOGI(kTag, "OTA confirmed after startup and authenticated HTTPS checks");
    return respond(request, "200 OK", "{\"status\":\"confirmed\"}", false);
}

void trial_timeout(void *)
{
    if (!pending.exchange(false)) return;
    ESP_LOGE(kTag, "OTA trial timed out without confirmation");
    esp_restart();
}
}

void initialize()
{
    esp_ota_img_states_t state;
    pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
              state == ESP_OTA_IMG_PENDING_VERIFY;
    if (pending) {
        esp_timer_create_args_t args{};
        args.callback = trial_timeout;
        args.name = "ota_trial";
        if (esp_timer_create(&args, &trial_timer) != ESP_OK ||
            esp_timer_start_once(trial_timer, 90 * UINT64_C(1000000)) != ESP_OK)
            esp_restart();
    }
    layout_ok = validate_layout();
    if (!layout_ok) {
        ESP_LOGE(kTag, "U213 OTA partition table missing or incompatible; install the USB package");
        return;
    }
    // The TLS server and provisioning DRBG both need hardware entropy.
    // This diagnostic profile has no other ADC or RF clients.
    bootloader_random_enable();
    const auto config_result = config_open();
    config_ok = config_result == ESP_OK;
    const auto credential_result = credentials_open();
    credentials_ok = credential_result == ESP_OK;
    if (!config_ok) ESP_LOGE(kTag, "Configuration unavailable: %s", esp_err_to_name(config_result));
    if (!credentials_ok) ESP_LOGE(kTag, "OTA identity unavailable: %s", esp_err_to_name(credential_result));
    if (config_ok && !pending) reconcile_update();
}

esp_err_t start_https(esp_err_t (*health_handler)(httpd_req_t *))
{
    if (!layout_ok || !config_ok || !credentials_ok || !provisioned()) return ESP_ERR_INVALID_STATE;
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = reinterpret_cast<const uint8_t *>(certificate());
    config.servercert_len = std::strlen(certificate()) + 1;
    config.prvtkey_pem = reinterpret_cast<const uint8_t *>(private_key());
    config.prvtkey_len = std::strlen(private_key()) + 1;
    config.httpd.stack_size = 12288;
    config.httpd.max_open_sockets = 3;
    config.httpd.recv_wait_timeout = 8;
    config.httpd.send_wait_timeout = 8;
    config.httpd.lru_purge_enable = true;

    auto result = httpd_ssl_start(&https_server, &config);
    if (result != ESP_OK) return result;
    httpd_uri_t route{};
    route.uri = "/health";
    route.method = HTTP_GET;
    route.handler = health_handler;
    result = httpd_register_uri_handler(https_server, &route);
    if (result == ESP_OK) {
        route.uri = "/ota";
        route.method = HTTP_POST;
        route.handler = upload;
        result = httpd_register_uri_handler(https_server, &route);
    }
    if (result == ESP_OK) {
        route.uri = "/ota/confirm";
        route.handler = confirm;
        result = httpd_register_uri_handler(https_server, &route);
    }
    if (result != ESP_OK) {
        httpd_ssl_stop(https_server);
        https_server = nullptr;
    }
    return result;
}

void verify_boot(bool services_ok)
{
    if (!pending) return;
    if (services_ok && https_server && config_ok && credentials_ok && provisioned() &&
        !kFailTrial) {
        startup_checked = true;
        ESP_LOGI(kTag, "OTA startup checks passed; waiting for authenticated HTTPS confirmation");
        return;
    }
    ESP_LOGE(kTag, "OTA startup checks failed; rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
    // A reset also causes the bootloader to abort a still-pending image.
    esp_restart();
}

int health_fields(char *output, std::size_t capacity)
{
    UpdateRecord record{};
    const auto result = config_ok ? read_update(record) : ESP_ERR_INVALID_STATE;
    char build[65];
    hex_encode(esp_app_get_description()->app_elf_sha256, 32, build);
    return std::snprintf(output, capacity,
        ",\"build_id\":\"%s\",\"uptime_s\":%" PRIi64 ",\"running_partition\":\"%s\","
        "\"ota_image_state\":\"%s\",\"last_update_result\":\"%s\",\"ota_provisioned\":%s,\"ota_trial_ready\":%s",
        build, esp_timer_get_time() / INT64_C(1000000), esp_ota_get_running_partition()->label,
        state_name(), result == ESP_OK ? result_name(record.result) : "journal_unavailable",
        provisioned() ? "true" : "false", startup_checked && pending ? "true" : "false");
}

int console_command(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[0], "ota") == 0 && std::strcmp(argv[1], "provision") == 0) {
        const auto result = config_ok && credentials_ok && !pending && !provisioned_this_boot ?
                            provision() : ESP_ERR_INVALID_STATE;
        if (result == ESP_OK) provisioned_this_boot = true;
        else std::printf("ota_provision_error=%s\n", esp_err_to_name(result));
        return result == ESP_OK ? 0 : 1;
    }
    if (argc == 1 && std::strcmp(argv[0], "ota") == 0) {
        char status[320];
        health_fields(status, sizeof(status));
        std::printf("OTA status: {%s}\n", status + 1);
        return 0;
    }
    if (std::strcmp(argv[0], "config") == 0 && argc == 1) {
        if (config_ok) config_print();
        else std::puts("config_error=storage_unavailable");
        return config_ok ? 0 : 1;
    }
    if (std::strcmp(argv[0], "config") == 0 && argc == 3 && std::strcmp(argv[1], "probe") == 0 &&
        argv[2][0] >= '0' && argv[2][0] <= '9') {
        char *end;
        errno = 0;
        const auto value = std::strtoull(argv[2], &end, 10);
        if (errno == 0 && *end == '\0' && value <= UINT32_MAX && config_ok && !pending) {
            const auto result = config_probe(static_cast<uint32_t>(value));
            if (result == ESP_OK) config_print();
            else std::printf("config_error=%s\n", esp_err_to_name(result));
            return result == ESP_OK ? 0 : 1;
        }
    }
    std::puts("Use ota, ota provision, config, or config probe <uint32>.");
    return 1;
}
}
