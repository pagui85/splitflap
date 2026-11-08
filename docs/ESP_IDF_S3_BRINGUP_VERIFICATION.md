# Milestone 3 standalone S3 bring-up verification

Date: 2026-11-08. Parent `feature/esp-idf-s3` at
`0a8ebf8367029dc80d793286d05265762298a925`; new branch
`feature/esp-idf-s3-bringup`. Standalone application:
`firmware/esp-idf-bringup`. Existing native ESP32/ESP32-S3 and legacy PlatformIO
application code is unchanged; only the old native README adds a discovery link.

## Native builds: PASS

ESP-IDF v5.5.6, SDK commit `880a203ae603d08a94d06aa01e0d6761958e5e9e`;
Xtensa GCC 14.2.0 / crosstool-NG `esp-14.2.0_20260121`; Python 3.12.14;
CMake 4.4.4; Ninja `1.13.2.git.kitware.jobserver-pipe-1`; esptool 4.13.0.
SDK tools were installed with `./install.sh esp32,esp32s3` and `export.sh`.

From `firmware/esp-idf-bringup`, clean build-local configuration:

```sh
idf.py -B build-s3-bringup set-target esp32s3
idf.py -B build-s3-bringup build
idf.py -B build-s3-bringup size
python tests/check_isolation.py build-s3-bringup
```

All commands exited **0**. The diagnostic application is **289904 bytes**
(`0x46c70`), with `0xb9390` bytes / 72% free in the 1 MiB factory slot.
Bootloader: **20464 bytes** (`0x4ff0`), 38% free. IDF partition checks pass.
The final configure/build logs contain no compiler warnings or errors.

From `firmware/esp-idf`, both original native target configurations were rebuilt:

```sh
idf.py -B build-esp32 set-target esp32
idf.py -B build-esp32 build
idf.py -B build-esp32 size
idf.py -B build-esp32s3 set-target esp32s3
idf.py -B build-esp32s3 build
idf.py -B build-esp32s3 size
```

All six commands exited **0**, with no compiler warnings/errors in the final logs.

| Project/target | App `.bin` | Bootloader | Real-IDF compile entries |
| --- | --- | --- | --- |
| Standalone diagnostics / ESP32-S3 | 289904 bytes | 20464 bytes | 512 |
| Existing native / ESP32 | 156848 bytes | 26000 bytes | 466 |
| Existing native / ESP32-S3 | 183408 bytes | 20464 bytes | 506 |

UTC metadata is generated for initial/changed application or configuration inputs;
SOURCE_DATE_EPOCH can make it repeatable. A no-change rebuild/size must retain the
same binary/header (verified before publication). The isolation check confirms
the generated UTC string is embedded
in the actual binary, not merely present in a header. Build timestamp affects
image content, so no cross-build byte-identical hash claim is made.

## Host tests and offline tools: PASS

From repository root:

```sh
bash firmware/esp-idf-bringup/tests/run_host_tests.sh
bash firmware/esp-idf/tests/run_host_tests.sh
```

Both scripts exited **0**. The new C++17 suite passes strict command parsing,
valid/invalid UTC calendars including leap days and bounds, approximate/retained/
manual clock provenance, setter failure/idempotence, EAP authorization always
false, package PSRAM-code decoding, and reserved/duplicate pin conflict checks.
The Python suite proves UTC output independent of local timezone, deterministic
SOURCE_DATE_EPOCH output/no needless rewrite, and rejection of invalid epochs.
Existing register/loopback/pin/HAL fake-driver tests also pass for both targets.

`check_isolation.py` passes against the built diagnostic: no Chainlink HAL, SPI
master, Wi-Fi/supplicant, legacy source or GPIO/SPI2/PSRAM activation calls;
PSRAM off, UART0 default, all five reported assignments -1. Normal memory-bus and
UART driver code is expected. No NVS/filesystem initialization or persistent
clock/history writes are performed by the application.

Offline `python -m esptool --help`, `chip_id --help`, `flash_id --help` and
`read_flash --help` confirm the SDK's documented command spellings and the `ALL`
backup-length option. Offline `image_info` validates the generated ESP32-S3 app
checksum and SHA256 image hash. Generated `flash_args` specifies DIO, 40 MHz,
2 MiB envelope with S3 bootloader `0x0`, partition table `0x8000`, app `0x10000`.
**No flash, monitor, chip_id, flash_id or read_flash command was executed against
a serial device.** Help and binary inspection do not test hardware commands.

`git diff --check`, legacy/native-code preservation, ignored build/credential
paths and relative documentation links are checked before publication. No
generated binaries, backups, keys, certificates or credentials are committed.

## Remote CI

The workflow retains both original native target builds and host tests and adds
an independent S3 diagnostic build/size/isolation job in official
`espressif/idf:v5.5.6`. Results are pending publication/run at this revision and
will be recorded before handoff; a workflow definition is not a passing run.

## Hardware limits and outstanding observations

There was no board connection, flashing, UART interaction, memory probing or
reset/reboot observation by the worker. Console operation on the actual UART
bridge, physical flash detection, package eFuse contents, reset reasons, clock
holdover and stack/heap margins remain owner tests. No fake hardware outputs
are presented as measurements.

PSRAM is deliberately uninitialized in this unknown-board image. Read-only
package eFuse hints can indicate embedded capacity; zero/unknown does not prove
absence of separately fitted RAM. Unknown quad/octal mode or memory voltage is
not guessed. Physical PSRAM usability/size needs a later matched configuration.
Likewise a conservative DIO/40 MHz image is not universal proof of OPI-flash or
unusual-board compatibility; preflight and recovery instructions state the gate.

Time is approximate or manually provisioned for development only; EAP-TLS is
absent and never authorized by compile time or manual provisioning. No harness,
motor, RTC, calibration, PCB, Wi-Fi, master/upstream write or merge is involved.
See [owner build/flash/console/recovery checklist](../firmware/esp-idf-bringup/README.md).
