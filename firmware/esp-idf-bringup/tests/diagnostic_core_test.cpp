// SPDX-License-Identifier: Apache-2.0
#include "diagnostic_core.hpp"
#include <cassert>
#include <initializer_list>
#include <iostream>
#include <vector>
namespace {
std::int64_t applied = -1;
int calls = 0;
bool accept = true;
bool setter(std::int64_t epoch) { ++calls; if (accept) applied = epoch; return accept; }
bringup::Request parse(std::initializer_list<const char *> values)
{
    std::vector<char *> argv;
    for (const char *value : values) argv.push_back(const_cast<char *>(value));
    return bringup::parse_command(static_cast<int>(argv.size()), argv.data());
}
}
int main()
{
    using namespace bringup;
    std::int64_t epoch = -1;
    assert(parse_utc("1970-01-01T00:00:00Z", epoch) && epoch == 0);
    assert(parse_utc("2000-02-29T00:00:00Z", epoch) && epoch == 951782400);
    assert(parse_utc("2024-02-29T00:00:00Z", epoch) && epoch == 1709164800);
    assert(parse_utc("2099-12-31T23:59:59Z", epoch) && epoch == kLastSupportedEpoch);
    for (const char *bad : {"", "1969-12-31T23:59:59Z", "2100-01-01T00:00:00Z",
        "2023-02-29T00:00:00Z", "2024-04-31T00:00:00Z", "2024-00-01T00:00:00Z",
        "2024-13-01T00:00:00Z", "2024-01-00T00:00:00Z", "2024-01-01T24:00:00Z",
        "2024-01-01T00:60:00Z", "2024-01-01T00:00:60Z", "2024-01-01T00:00:00z",
        "2024-01-01 00:00:00Z", "2024-01-01T00:00:00Zjunk", "2024-01-01T00:00:00+00:00"}) {
        epoch = -99;
        assert(!parse_utc(bad, epoch) && epoch == -99);
    }
    assert(parse({"info"}).command == Command::Info);
    assert(parse({"pins"}).command == Command::Pins);
    assert(parse({"spi"}).command == Command::Spi);
    assert(parse({"time"}).command == Command::Time);
    assert(parse({"reboot"}).command == Command::Reboot);
    assert(parse({"time", "set", "2024-02-29T00:00:00Z"}).epoch == 1709164800);
    assert(parse({"time", "set", "2024-02-29T00:00:00Z"}).command == Command::SetTime);
    assert(parse({"spi", "init"}).command == Command::Invalid);
    assert(parse({"pins", "enable"}).command == Command::Invalid);
    assert(parse({"reboot", "extra"}).command == Command::Invalid);
    assert(parse({"time", "set"}).command == Command::Invalid);
    assert(parse({"time", "set", "bad"}).command == Command::Invalid);
    assert(parse({"unknown"}).command == Command::Invalid);
    assert(parse({nullptr}).command == Command::Invalid);
    ClockState state;
    assert(state.bootstrap(1709164800, 0, setter));
    assert(state.source() == TimeSource::BuildApproximate && applied == 1709164800);
    assert(!state.provisioned() && !state.eap_authorized());
    accept = false;
    assert(!state.provision(1709164801, setter) && !state.provisioned());
    accept = true;
    assert(state.provision(1709164801, setter) && state.provisioned() && !state.eap_authorized());
    const int before = calls;
    assert(state.bootstrap(1709164800, 0, setter) && calls == before && state.provisioned());
    assert(!state.provision(-1, setter) && !state.provision(kLastSupportedEpoch + 1, setter));
    ClockState retained;
    assert(retained.bootstrap(1709164800, 1709164900, setter) && calls == before);
    assert(retained.source() == TimeSource::RetainedUnverified && !retained.provisioned());
    ClockState failed;
    accept = false;
    assert(!failed.bootstrap(1709164800, 0, setter) && failed.source() == TimeSource::Unset);
    assert(!failed.eap_authorized());
    assert(package_psram_bytes(1) == 8388608 && package_psram_bytes(2) == 2097152);
    assert(package_psram_bytes(3) == 16777216 && package_psram_bytes(4) == 4194304);
    for (unsigned code : {0, 5, 6, 7, 255}) assert(package_psram_bytes(code) == 0);
    const int empty[] = {-1, -1, -1, -1, -1}, candidate[] = {11, 13, 12, 10, -1};
    const int duplicate[] = {11, 13, 12, 11, -1}, uart[] = {43, 13, 12, 10, -1};
    for (unsigned i = 0; i < 5; ++i) { assert(!pin_conflict(empty, 5, i)); assert(!pin_conflict(candidate, 5, i)); }
    assert(pin_conflict(duplicate, 5, 0) && pin_conflict(duplicate, 5, 3));
    assert(pin_conflict(uart, 5, 0) && pin_conflict(nullptr, 0, 0));
    std::cout << "PASS: console parsing, strict UTC calendar, clock provenance/failures, PSRAM hints and pin conflicts\n";
}
