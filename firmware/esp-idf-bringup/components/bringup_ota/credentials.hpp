// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace bringup::ota {
inline constexpr std::size_t kDigestSize = 32;
void hex_encode(const uint8_t *bytes, std::size_t size, char *output);
bool hex_decode(const char *text, std::size_t size, uint8_t *output);
esp_err_t credentials_open();
bool provisioned();
esp_err_t provision();
bool authorized(const char *token);
const char *certificate();
const char *private_key();
}
