// SPDX-License-Identifier: Apache-2.0
// Chainlink register layout derived from Scott Bezek's spi_io_config.h (Apache-2.0).
#pragma once
#include <cstddef>
#include <cstdint>

namespace splitflap::chainlink {
constexpr std::size_t kMaxModules = 252; // Multiple of six within legacy uint8_t indexing.
constexpr bool valid_capacity(std::size_t n) { return n >= 6 && n <= kMaxModules && n % 6 == 0; }
constexpr std::size_t output_bytes(std::size_t n) { return valid_capacity(n) ? n / 6 * 4 : 0; }
constexpr std::size_t sensor_bytes(std::size_t n) { return valid_capacity(n) ? n / 6 : 0; }
constexpr std::size_t loopback_count(std::size_t n) { return valid_capacity(n) ? n / 3 : 0; }

struct Bit { std::size_t byte; uint8_t mask; };
constexpr Bit loopback_output(std::size_t modules, std::size_t index)
{
    return {output_bytes(modules) - 1 - index / 2 * 4 - (index % 2 == 0 ? 1 : 2),
            static_cast<uint8_t>(index % 2 == 0 ? 0x80 : 0x08)};
}
constexpr Bit loopback_input(std::size_t index)
{
    return {index / 2, static_cast<uint8_t>(index % 2 == 0 ? 0x40 : 0x80)};
}
// Helpers reject invalid indexes before using the layout above. Callers must
// allow >=2 complete exchange cycles after setting a loopback before checking it.
inline bool set_loopback(uint8_t *out, std::size_t size, std::size_t modules,
                         std::size_t index, bool on)
{
    if (!out || !valid_capacity(modules) || size != output_bytes(modules) ||
        index >= loopback_count(modules)) return false;
    const auto bit = loopback_output(modules, index);
    if (on) out[bit.byte] |= bit.mask;
    else out[bit.byte] &= static_cast<uint8_t>(~bit.mask);
    return true;
}
inline bool loopback_matches(const uint8_t *in, std::size_t size, std::size_t modules,
                             std::size_t enabled_index)
{
    if (!in || !valid_capacity(modules) || size != sensor_bytes(modules) ||
        enabled_index >= loopback_count(modules)) return false;
    for (std::size_t i = 0; i < size; ++i) {
        const auto expected = i == enabled_index / 2 ? loopback_input(enabled_index).mask : 0;
        if ((in[i] & 0xc0) != expected) return false;
    }
    return true;
}
// In forward wire-buffer order: two whole coil bytes and two mixed coil/LED/
// loopback bytes per driver. This milestone permits only LED/loopback bits.
inline bool coils_are_off(const uint8_t *out, std::size_t size, std::size_t modules)
{
    if (!out || !valid_capacity(modules) || size != output_bytes(modules)) return false;
    constexpr uint8_t masks[] = {0xff, 0xf0, 0x0f, 0xff};
    for (std::size_t i = 0; i < size; ++i) if (out[i] & masks[i % 4]) return false;
    return true;
}
} // namespace splitflap::chainlink
