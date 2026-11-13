# Native ESP-IDF scaffold — ESP32, ESP32-S3 and ESP32-P4

For **standalone USB-UART board identification and an interactive console**, use
the separate [milestone 3 bring-up application](../esp-idf-bringup/README.md).
For P4 board testing and Ethernet, use the [P4 bring-up guide](../../docs/ESP_IDF_P4_BRINGUP.md).

This isolated C++/FreeRTOS application initializes NVS without erase-on-error and
runs a health task/events. The Chainlink SPI/GPIO component is compiled for all three
targets but **never initialized by app_main**. No motors, sensors, LCD, serial
protocol, Wi-Fi, HTTP or OTA service is started. No hardware functionality is
claimed from compilation. Original PlatformIO files/dependencies are unchanged.

## Reproduce the builds

Use Espressif ESP-IDF **v5.5.6**, SDK commit
`880a203ae603d08a94d06aa01e0d6761958e5e9e`. The 5.5 maintenance line remains
supported; no compatibility issue required changing milestone 1's release.
Install Git, Python, host compiler and the SDK's documented prerequisites.

```sh
# Outside this repository, once:
git clone --branch v5.5.6 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v5.5.6"
cd "$HOME/esp/esp-idf-v5.5.6"
./install.sh esp32,esp32s3,esp32p4
. ./export.sh

cd "$HOME/Projects/splitflap/firmware/esp-idf"
idf.py -B build-esp32 set-target esp32
idf.py -B build-esp32 build
idf.py -B build-esp32 size

idf.py -B build-esp32s3 set-target esp32s3
idf.py -B build-esp32s3 build
idf.py -B build-esp32s3 size

# P4 v3.x compile check. Use the standalone bring-up project on the board.
idf.py -B build-esp32p4 -D IDF_TARGET=esp32p4 build
idf.py -B build-esp32p4 size
```

CMake places `sdkconfig` inside each build directory, so targets do not overwrite
one another's configuration. ESP-IDF loads `sdkconfig.defaults` and then
`sdkconfig.defaults.<target>` automatically. Use a fresh build directory for a
clean configuration; existing sdkconfig choices can override changed defaults.
The old `idf.py set-target esp32 && idf.py build` default-directory workflow also
works; separate directories are recommended when retaining multiple targets.
No certificate, Arduino core, managed component or splitflap submodule is needed.

| Setting | ESP32 | ESP32-S3 |
| --- | --- | --- |
| Generated configuration | `build-esp32/sdkconfig` | `build-esp32s3/sdkconfig` |
| Flash image envelope | 2 MiB header, DIO, 40 MHz | 2 MiB header, DIO, 40 MHz |
| PSRAM | Disabled; internal RAM only | Disabled; no capacity/mode assumed |
| Partition selection | IDF factory-only single-app table | Same reviewed diagnostic-only table |
| Application console | UART0, 115200 | UART0, 115200; secondary USB console off |
| Chainlink GPIO defaults | Unassigned, inactive | Unassigned, inactive |

**These are compile-only choices, not measured board specifications.** The S3
may require different flash mode/frequency/voltage and has unconfirmed flash and
PSRAM capacities. Do not infer N8R8/N16R8. UART0 normally uses GPIO43/44 on S3;
confirm the exact board's UART bridge/connector before a later console session.
USB GPIO19/20 are not reassigned to the harness. No eFuse changes are involved.

Both builds use NVS `0x9000`/24 KiB, PHY init `0xf000`/4 KiB, factory app
`0x10000`/1 MiB. They have **no FFat and no OTA slots**. This table changes the
legacy NVS extent and does not preserve its FFat/OTA layout. Do not flash it over
the working controller or migrate calibration implicitly.

## Components and test boundary

- `main`: ordered NVS/event/runtime startup, no hardware activation switch.
- `scaffold_platform`: NVS initialization; stops on errors without erasing.
- `scaffold_runtime`: a diagnostic task and copied fixed-size health events;
  not a timing template for motor stepping.
- `chainlink_bus`: explicit configurable pins, private SPI2, single-owner polling
  transfers, optional disable-only OE, logic-only coil guard and staged cleanup.
  Unused HAL functions may be removed by the linker; compiling them does not run
  them or validate their electrical behavior.

Run portable protocol/pin-policy and fake-driver checks from repository root:

```sh
bash firmware/esp-idf/tests/run_host_tests.sh
```

The host model compiles the actual HAL implementation for each pin-policy target
against test doubles, checking TX/RX ordering, no transfers on initialization,
coil rejection, timeouts, allocation/device failures, ownership and cleanup retry.
It does not emulate electrical behavior, DMA hardware, interrupts or actual IDF
driver internals. Native builds separately compile against real IDF headers.

The independent CI matrix builds both targets in `espressif/idf:v5.5.6` and runs
these host tests. It checks the SDK commit and does not provision, flash or upload
firmware artifacts. Legacy build command remains `pio run -e chainlink` at root.
See [milestone 2 verification](../../docs/ESP_IDF_S3_VERIFICATION.md) for exact
versions/results; [milestone 1 results](../../docs/ESP_IDF_VERIFICATION.md) remain
historical evidence, not current S3 claims.

## Hardware and next steps

Start with the [Buddy source audit and provisional logical harness](../../docs/ESP_IDF_S3_HARDWARE.md)
and [owner verification gates](../../docs/ESP_IDF_S3_BENCH_CHECKLIST.md).
No final wiring diagram is issued until the physical board pinout is confirmed.
The existing Driver boards and IDC chain stay unchanged; no routed OE exists on
the stock Buddy. A GPIO-free startup cannot guarantee de-energized retained coils.

[EAP-TLS/time/RTC investigation](../../docs/ESP_IDF_TIME_TRUST.md) preserves strict
CA, RADIUS identity and validity-time checks. It remains a plan, with networking
inactive. Motor/serial parity, calibration import, authenticated HTTP and OTA are
future milestones; see the [migration audit](../../docs/ESP_IDF_MIGRATION_AUDIT.md).
