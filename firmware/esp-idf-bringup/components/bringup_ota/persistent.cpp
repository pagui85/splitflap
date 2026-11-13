// SPDX-License-Identifier: Apache-2.0
#include "persistent.hpp"
#include <array>
#include <cinttypes>
#include <cstdio>
#include "nvs.h"
#include "nvs_flash.h"

namespace bringup::ota {
namespace {
// V1 storage is little-endian: version, test marker, flap count, channel
// count, followed by 72 ordered motor-step offsets. A zero count is empty.
constexpr char kNamespace[] = "splitflap";
constexpr char kConfigKey[] = "settings";
constexpr char kUpdateKey[] = "ota_last";
constexpr uint32_t kVersion = 1;
using Settings = std::array<uint8_t, 12 + kChannelCapacity * 2>;
nvs_handle_t storage = 0;

uint32_t read_le(const uint8_t *p, unsigned bytes)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= uint32_t(p[i]) << (8 * i);
    return value;
}

void write_le(uint8_t *p, uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}

esp_err_t load_settings(Settings &settings)
{
    size_t size = settings.size();
    const auto result = nvs_get_blob(storage, kConfigKey, settings.data(), &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        settings = {};
        write_le(settings.data(), kVersion, 4);
        return ESP_OK;
    }
    if (result != ESP_OK) return result;
    if (size != settings.size() || read_le(settings.data(), 4) != kVersion)
        return ESP_ERR_INVALID_VERSION;
    const auto count = read_le(settings.data() + 10, 2);
    if (count > kChannelCapacity || (count != 0 && read_le(settings.data() + 8, 2) == 0))
        return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
}

esp_err_t config_open()
{
    auto result = nvs_flash_init_partition(kConfigPartition);
    if (result != ESP_OK) return result;
    result = nvs_open_from_partition(kConfigPartition, kNamespace, NVS_READWRITE, &storage);
    if (result != ESP_OK) return result;
    Settings settings{};
    return load_settings(settings);
}

esp_err_t config_probe(uint32_t value)
{
    Settings settings{};
    auto result = load_settings(settings);
    if (result != ESP_OK) return result;
    write_le(settings.data() + 4, value, 4);
    result = nvs_set_blob(storage, kConfigKey, settings.data(), settings.size());
    return result == ESP_OK ? nvs_commit(storage) : result;
}

void config_print()
{
    Settings settings{};
    auto result = load_settings(settings);
    if (result != ESP_OK) {
        std::printf("config_error=%s\n", esp_err_to_name(result));
        return;
    }
    std::printf("config_version=%" PRIu32 " probe=%" PRIu32 " flap_count=%" PRIu32
                " calibrated_channels=%" PRIu32 " channel_capacity=%u\n", read_le(settings.data(), 4),
                read_le(settings.data() + 4, 4), read_le(settings.data() + 8, 2),
                read_le(settings.data() + 10, 2), kChannelCapacity);
}

esp_err_t read_update(UpdateRecord &record)
{
    static_assert(sizeof(UpdateRecord) == 36);
    size_t size = sizeof(record);
    const auto result = nvs_get_blob(storage, kUpdateKey, &record, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        record = {};
        return ESP_OK;
    }
    if (result != ESP_OK) return result;
    if (size != sizeof(record) || record.version != 1 || record.result > Result::BootError)
        return ESP_ERR_INVALID_VERSION;
    return ESP_OK;
}

esp_err_t save_update(const UpdateRecord &record)
{
    auto result = nvs_set_blob(storage, kUpdateKey, &record, sizeof(record));
    return result == ESP_OK ? nvs_commit(storage) : result;
}

const char *result_name(Result result)
{
    switch (result) {
    case Result::None: return "none";
    case Result::Interrupted: return "interrupted";
    case Result::RejectedImage: return "rejected_image";
    case Result::WriteError: return "write_error";
    case Result::Staged: return "staged";
    case Result::Confirmed: return "confirmed";
    case Result::RolledBack: return "rolled_back";
    case Result::BootError: return "boot_error";
    }
    return "unknown";
}
}
