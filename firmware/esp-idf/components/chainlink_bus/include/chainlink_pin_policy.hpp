// SPDX-License-Identifier: Apache-2.0
#pragma once
namespace splitflap::chainlink {
enum class Chip { Esp32, Esp32S3, Esp32P4 };

constexpr bool p4_reserved_pin(int pin)
{
    // USB Serial/JTAG and bootstraps (including UART0 TX/RX).
    return pin == 24 || pin == 25 || (pin >= 34 && pin <= 38);
}

constexpr bool allowed_pin(Chip chip, int pin, bool output)
{
    // No P4 board or Chainlink wiring has been verified.
    if (chip == Chip::Esp32P4) return false;
    if (chip == Chip::Esp32S3)
        return pin == 1 || pin == 2 || (pin >= 4 && pin <= 18) || pin == 21;
    // ESP32 GPIO34..39 are input-only. Avoid external memory pins 16/17 too.
    if (output && pin >= 34) return false;
    return pin == 4 || pin == 13 || pin == 14 || pin == 18 || pin == 19 ||
           (pin >= 21 && pin <= 23) || (pin >= 25 && pin <= 27) ||
           (pin >= 32 && pin <= 39);
}
}
