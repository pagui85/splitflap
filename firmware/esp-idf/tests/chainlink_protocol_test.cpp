// SPDX-License-Identifier: Apache-2.0
#include "chainlink_protocol.hpp"
#include "chainlink_pin_policy.hpp"
#include <array>
#include <cassert>
#include <iostream>

using namespace splitflap::chainlink;
int main()
{
    // Golden vectors from legacy initialize_modules/loopback register wiring.
    static_assert(output_bytes(6) == 4 && sensor_bytes(6) == 1);
    static_assert(output_bytes(48) == 32 && sensor_bytes(48) == 8);
    static_assert(output_bytes(72) == 48 && sensor_bytes(72) == 12);
    static_assert(loopback_output(6, 0).byte == 2 && loopback_output(6, 0).mask == 0x80);
    static_assert(loopback_output(6, 1).byte == 1 && loopback_output(6, 1).mask == 0x08);
    static_assert(loopback_output(12, 2).byte == 2 && loopback_output(12, 3).byte == 1);
    static_assert(!valid_capacity(0) && !valid_capacity(7) && !valid_capacity(255));
    for (std::size_t modules : {6, 12, 48, 72, 252}) {
        std::array<uint8_t, 168> out{};
        std::array<uint8_t, 42> in{};
        const auto count = output_bytes(modules);
        for (std::size_t i = 0; i < loopback_count(modules); ++i) {
            assert(set_loopback(out.data(), count, modules, i, true));
            assert(coils_are_off(out.data(), count, modules));
            const auto bit = loopback_input(i);
            in[bit.byte] = bit.mask | 0x3f; // Home sensors must not affect loopback validation.
            assert(loopback_matches(in.data(), sensor_bytes(modules), modules, i));
            in[bit.byte] ^= 0xc0; // Other loopback shorted/set instead: must reject.
            assert(!loopback_matches(in.data(), sensor_bytes(modules), modules, i));
            in[bit.byte] = 0;
            assert(set_loopback(out.data(), count, modules, i, false));
        }
        assert(!set_loopback(out.data(), count, modules, loopback_count(modules), true));
        // Test every wire output bit against independent golden group bytes:
        // byte0/3 all coils, byte1 high nibble coils, byte2 low nibble coils.
        for (std::size_t byte = 0; byte < count; ++byte) {
            for (unsigned bit = 0; bit < 8; ++bit) {
                out[byte] = 1u << bit;
                const bool expected_off = (byte % 4 == 1 && bit < 4) || (byte % 4 == 2 && bit >= 4);
                assert(coils_are_off(out.data(), count, modules) == expected_off);
                out[byte] = 0;
            }
        }
    }
    assert(!coils_are_off(nullptr, 4, 6));
    std::array<uint8_t, 4> out{};
    assert(!coils_are_off(out.data(), 3, 6));
    for (int pin : {0, 3, 19, 20, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
                    38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, -1, 49})
        assert(!allowed_pin(Chip::Esp32S3, pin, false));
    for (int pin : {10, 11, 12, 13}) assert(allowed_pin(Chip::Esp32S3, pin, true));
    for (int pin : {25, 32, 33}) assert(allowed_pin(Chip::Esp32, pin, true));
    assert(allowed_pin(Chip::Esp32, 39, false) && !allowed_pin(Chip::Esp32, 39, true));
    for (int pin : {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 15, 16, 17})
        assert(!allowed_pin(Chip::Esp32, pin, true));
    for (int pin = 0; pin <= 54; ++pin) {
        assert(!allowed_pin(Chip::Esp32P4, pin, false));
        assert(!allowed_pin(Chip::Esp32P4, pin, true));
    }
    for (int pin : {24, 25, 26, 27, 34, 35, 36, 37, 38}) assert(p4_reserved_pin(pin));
    std::cout << "PASS: Chainlink golden vectors, coil guard, loopback isolation and pin policy\n";
}
