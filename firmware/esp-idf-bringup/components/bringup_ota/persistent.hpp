// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include "esp_err.h"

namespace bringup::ota {
inline constexpr char kConfigPartition[] = "splitcfg";
inline constexpr char kKeysPartition[] = "otakeys";
inline constexpr unsigned kChannelCapacity = 72;

enum class Result : uint8_t {
    None, Interrupted, RejectedImage, WriteError, Staged, Confirmed, RolledBack, BootError
};

struct UpdateRecord {
    uint8_t version = 1;
    Result result = Result::None;
    uint8_t slot = 0xff;
    uint8_t reserved = 0;
    uint8_t build[32]{};
};

esp_err_t config_open();
esp_err_t config_probe(uint32_t value);
void config_print();
esp_err_t read_update(UpdateRecord &record);
esp_err_t save_update(const UpdateRecord &record);
const char *result_name(Result result);
}
