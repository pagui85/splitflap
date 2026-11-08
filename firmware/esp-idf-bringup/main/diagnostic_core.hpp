// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace bringup {
constexpr std::int64_t kLastSupportedEpoch = 4102444799LL; // 2099-12-31T23:59:59Z
bool parse_utc(std::string_view text, std::int64_t &epoch);
enum class Command { Invalid, Info, Pins, Spi, Time, SetTime, Reboot };
struct Request { Command command = Command::Invalid; std::int64_t epoch = 0; };
Request parse_command(int argc, char **argv);

enum class TimeSource { Unset, BuildApproximate, RetainedUnverified, ManualSerial };
using SetClock = bool (*)(std::int64_t epoch);
class ClockState {
public:
    bool bootstrap(std::int64_t build, std::int64_t existing, SetClock setter);
    bool provision(std::int64_t epoch, SetClock setter);
    TimeSource source() const { return source_; }
    bool provisioned() const { return source_ == TimeSource::ManualSerial; }
    bool eap_authorized() const { return false; } // No authentication policy implemented here.
    const char *source_name() const;
private:
    TimeSource source_ = TimeSource::Unset;
};

// Read-only factory package hint, not a probe for separately fitted RAM.
std::size_t package_psram_bytes(unsigned capacity_code);
bool pin_conflict(const int *pins, std::size_t count, std::size_t index);
} // namespace bringup
