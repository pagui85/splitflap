# Milestone 2 verification — ESP32 and ESP32-S3

Date: 2026-11-08. Parent branch `feature/esp-idf-modern` at
`b4cc6d6c2e6886e461486b0d5017bc923c2f22c5`; work isolated on
`feature/esp-idf-s3`. No device access, flashing, motor power, calibration change,
certificate provisioning, network handshake, RTC or PCB work was performed.

## Local native builds: PASS for both targets

SDK v5.5.6 at `880a203ae603d08a94d06aa01e0d6761958e5e9e`, installed using
`./install.sh esp32,esp32s3` and `export.sh` on Linux x86_64. Both target builds use
Xtensa GCC 14.2.0, crosstool-NG `esp-14.2.0_20260121`; Python 3.12.14;
CMake 4.4.4; Ninja `1.13.2.git.kitware.jobserver-pipe-1`; esptool 4.13.0 for image
generation only. Initial environment lacked CMake; installing the documented host
tool resolved that environment prerequisite, without relaxing SDK checks.

From `firmware/esp-idf`, clean target build directories/configurations:

```sh
idf.py -B build-esp32 set-target esp32
idf.py -B build-esp32 build
idf.py -B build-esp32 size
idf.py -B build-esp32s3 set-target esp32s3
idf.py -B build-esp32s3 build
idf.py -B build-esp32s3 size
```

All six commands exited **0**. Final builds include the explicit DIO/40 MHz
defaults and compile the real HAL source. Configure/build logs contain no compiler
warnings/errors. ESP32 completes 523 top-level Ninja steps; S3 completes 563.

| Result | ESP32 | ESP32-S3 |
| --- | --- | --- |
| Application `.bin` | 156848 bytes / `0x264b0` | 183408 bytes / `0x2cc70` |
| Factory app partition | `0x100000` (1 MiB) | `0x100000` (1 MiB) |
| Free app space | `0xd9b50` / 85% | `0xd3390` / 83% |
| Bootloader `.bin` | 26000 bytes / `0x6590`, 9% free | 20464 bytes / `0x4ff0`, 38% free |
| Size tool unpadded image | 156729 bytes | 183293 bytes |
| Real-IDF compile database entries | 466 | 506 |

Both builds pass partition-size checks. S3 memory report uses combined DIRAM
(57235 bytes used) plus 16384 bytes reserved IRAM; original ESP32 reports
54631 bytes IRAM and 12172 bytes static DRAM. These are linker reports, not task
stack/heap measurements and not directly equivalent memory categories.

Generated configuration checks pass: different build-local sdkconfig paths,
correct target strings, 2 MiB image header, DIO/40 MHz, PSRAM disabled, UART0 at
115200, S3 secondary USB console disabled. The compile databases include
`chainlink_bus.cpp` and no legacy `firmware/src`/`firmware/esp32`, Arduino or Wi-Fi
source. Unused HAL functions can be removed by linker garbage collection: the
binary does not run or electrically test the HAL. No board memory capacity is
inferred from these build settings.

## Host tests: PASS

```sh
bash firmware/esp-idf/tests/run_host_tests.sh
```

Exit **0** with host `g++ (Debian 14.2.0-19) 14.2.0`, C++17,
`-Wall -Wextra -Werror`:

```text
PASS: Chainlink golden vectors, coil guard, loopback isolation and pin policy
PASS: esp32 HAL fake-driver sequence, inactive startup, rejection, faults and cleanup
PASS: esp32s3 HAL fake-driver sequence, inactive startup, rejection, faults and cleanup
```

Golden/register tests cover capacities 6/12/48/72/252, coil-bit rejection across
every output bit, one-at-a-time loopback echo/isolation, home-sensor independence,
bad sizes/indexes and conservative reserved-pin policies. The actual HAL source
is also compiled/run with minimal fake IDF driver functions for both target
policies. Tests check no initialization transfer, mode3-before-mode2 sequencing,
unchanged wire bytes, RX latch callbacks, argument/owner rejection, TX/RX timeout
faults without stale sensor copy, allocation/second-device failures, resource
cleanup retry and repeated close. No hardware/DMA/timing or IDF-internal behavior
is established by these test doubles. GPIO/ISR failures and physical power/reset
conditions still need separate validation.

## Preservation and review checks

- `git diff --check` passes.
- Legacy `platformio.ini`, `firmware/esp32`, `firmware/src`, `firmware/lib`,
  `firmware/include`, `proto`, and `.github/workflows/pio.yml` remain byte-for-byte
  unchanged against both milestone 1 parent and fork master. The original native
  ESP32 target remains buildable. No legacy package pins or source filters changed.
- Legacy compilation was not repeated in this milestone because those inputs
  are unchanged; [milestone 1](ESP_IDF_VERIFICATION.md) records the successful
  `chainlink` build, including its existing warnings.
- Source/PCB pad-net cross-check establishes the documented connections;
  all documentation-relative links and credential/build ignore rules are checked.
  No build output, credential or actual certificate/key material is staged.

## CI

The updated workflow has a native ESP32/ESP32-S3 build matrix plus a host-test
job, uses official `espressif/idf:v5.5.6` and checks the exact SDK commit.
Remote results are pending publication/run at this report revision and will be
updated before handoff. A workflow definition alone is not a CI pass.

## Outstanding owner gates

Exact S3 identity/header/power/flash/PSRAM inventory; actual Buddy/Driver revision
and continuity checks; one-source logic-power budget/backfeed review; then a
separately authorized logic-only image/bench session with all +12V disconnected.
No final assembly drawing is issued. See [hardware audit](ESP_IDF_S3_HARDWARE.md)
and [owner checklist](ESP_IDF_S3_BENCH_CHECKLIST.md).

Strict EAP-TLS exact-name/time policy and negative handshakes remain unimplemented;
see [time/RTC analysis](ESP_IDF_TIME_TRUST.md). Motor state-machine/serial parity,
calibration/partition migration, authenticated HTTP and secure OTA are future
milestones. Compile and host/CI passes are not a hardware compatibility claim.
