// SPDX-License-Identifier: Apache-2.0
#include "diagnostic_core.hpp"
#include "chainlink_pin_policy.hpp"

namespace bringup {
namespace {
bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
int days_in_month(int year, int month)
{
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1] + (month == 2 && leap(year));
}
}
bool parse_utc(std::string_view s, std::int64_t &epoch)
{
    if (s.size() != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' ||
        s[13] != ':' || s[16] != ':' || s[19] != 'Z') return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16 || i == 19) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    const auto two = [&s](std::size_t i) { return (s[i] - '0') * 10 + s[i + 1] - '0'; };
    const int year = two(0) * 100 + two(2), month = two(5), day = two(8);
    const int hour = two(11), minute = two(14), second = two(17);
    if (year < 1970 || year > 2099 || month < 1 || month > 12 || day < 1 ||
        day > days_in_month(year, month) || hour > 23 || minute > 59 || second > 59) return false;
    std::int64_t days = 0;
    for (int y = 1970; y < year; ++y) days += leap(y) ? 366 : 365;
    for (int m = 1; m < month; ++m) days += days_in_month(year, m);
    days += day - 1;
    epoch = days * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}
Request parse_command(int argc, char **argv)
{
    if (argc < 1 || !argv) return {};
    for (int i = 0; i < argc; ++i) if (!argv[i]) return {};
    const std::string_view name{argv[0]};
    if (argc == 1) {
        if (name == "info") return {Command::Info};
        if (name == "pins") return {Command::Pins};
        if (name == "spi") return {Command::Spi};
        if (name == "time") return {Command::Time};
        if (name == "reboot") return {Command::Reboot};
    }
    if (name == "time" && argc == 3 && std::string_view{argv[1]} == "set") {
        std::int64_t epoch;
        if (parse_utc(argv[2], epoch)) return {Command::SetTime, epoch};
    }
    return {};
}
bool ClockState::bootstrap(std::int64_t build, std::int64_t existing, SetClock setter)
{
    if (build < 0 || build > kLastSupportedEpoch || !setter) return false;
    if (source_ != TimeSource::Unset) return true; // Never clobber a provisioned session.
    if (existing >= build && existing <= kLastSupportedEpoch) {
        source_ = TimeSource::RetainedUnverified;
        return true;
    }
    if (!setter(build)) return false;
    source_ = TimeSource::BuildApproximate;
    return true;
}
bool ClockState::provision(std::int64_t epoch, SetClock setter)
{
    if (epoch < 0 || epoch > kLastSupportedEpoch || !setter || !setter(epoch)) return false;
    source_ = TimeSource::ManualSerial;
    return true;
}
const char *ClockState::source_name() const
{
    switch (source_) {
        case TimeSource::BuildApproximate: return "build_utc_approximate";
        case TimeSource::RetainedUnverified: return "retained_system_time_unverified";
        case TimeSource::ManualSerial: return "manual_serial_development";
        default: return "unset";
    }
}
std::size_t package_psram_bytes(unsigned code)
{
    // Capacity codes used by Espressif esptool v4.13.0 ESP32S3ROM.
    constexpr std::size_t mib = 1024 * 1024;
    switch (code) {
        case 1: return 8 * mib;
        case 2: return 2 * mib;
        case 3: return 16 * mib;
        case 4: return 4 * mib;
        default: return 0; // Unknown or no factory-marked embedded RAM, NOT proven absence.
    }
}
bool pin_conflict(const int *pins, std::size_t count, std::size_t index)
{
    if (!pins || index >= count) return true;
    if (pins[index] == -1) return false;
    if (!splitflap::chainlink::allowed_pin(splitflap::chainlink::Chip::Esp32S3,
                                          pins[index], index != 1)) return true;
    for (std::size_t i = 0; i < count; ++i)
        if (i != index && pins[i] == pins[index]) return true;
    return false;
}
} // namespace bringup
