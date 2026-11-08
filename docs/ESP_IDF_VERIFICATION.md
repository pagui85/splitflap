# Milestone 1 verification

Date: 2026-11-08. Baseline: `a8a34c7ab1c9a4acfcc479990eff475af091b8f7` on
`feature/esp-idf-modern`. This report covers compilation and source review only;
there was no device access, flashing, boot test or network handshake.

## Native build: PASS

Linux x86_64 environment, using Espressif's recursive SDK checkout and
`./install.sh esp32`/`export.sh`:

| Tool | Verified version |
| --- | --- |
| ESP-IDF | v5.5.6, commit `880a203ae603d08a94d06aa01e0d6761958e5e9e` |
| Target | `esp32` |
| Xtensa C++ compiler | GCC 14.2.0, crosstool-NG `esp-14.2.0_20260121` |
| Python | 3.12.14 in the IDF virtual environment |
| CMake | 4.4.4 |
| Ninja | `1.13.2.git.kitware.jobserver-pipe-1` |
| esptool | 4.13.0 (image generation only) |

From `firmware/esp-idf`, with no generated build/configuration present:

```sh
idf.py set-target esp32
idf.py build
idf.py size
```

All three commands exited **0**. The clean build completed 514 top-level Ninja
steps, linked `splitflap_scaffold.elf`, generated bootloader/partition/app images,
and passed ESP-IDF partition size checks. There were no compiler warning/error
matches in the final configure/build logs.

```text
Bootloader binary size 0x6590 bytes. 0xa70 bytes (9%) free.
splitflap_scaffold.bin binary size 0x26430 bytes.
Smallest app partition is 0x100000 bytes. 0xd9bd0 bytes (85%) free.
Project build complete.
```

Application `.bin` size is **156720 bytes**; size tool's unpadded total image is
156601 bytes, IRAM use 54631 bytes, static DRAM use 12172 bytes.
These are compile/link figures, not runtime heap/stack measurements.
The generated table is NVS `0x9000`/24 KiB, PHY init `0xf000`/4 KiB, factory
app `0x10000`/1 MiB; image header is 2 MiB flash/DIO/40 MHz.

An initial configure attempt was made while the SDK's recursive clone was still
in progress and failed with `Missing esp-mqtt submodule`. After the clone
finished, IDF refused to auto-delete that incomplete non-CMake build directory.
Only the scaffold's generated build directory was removed and configuration and
build were rerun successfully. No SDK, legacy source or security settings were
patched to obtain this pass.

## Legacy build: pending dependency installation

Attempted from repository root using PlatformIO Core 6.2.0/Python 3.12.14:

```sh
python -m platformio run -e chainlink
```

The original `espressif32@3.4.0`, toolchain `2.50200.97` and Arduino framework
`3.10006.210326` installed. PlatformIO rejected checksum mismatches from package
mirrors and obtained these packages from alternate mirrors without disabling
validation. Installation of `tool-esptoolpy ~1.30100.0` is still in progress at
this report revision. **No legacy compilation pass is claimed.** This section
will be updated with the final attempted build result before handoff.

## Isolation and review checks

- `git diff --check` passed.
- `git diff --exit-code a8a34c7 -- platformio.ini firmware/esp32 firmware/src
  firmware/lib firmware/include proto .github/workflows/pio.yml` passed: those
  legacy inputs are byte-for-byte unchanged.
- Native compile database inspection passed: none of its 460 source compilation
  commands include legacy `firmware/esp32`, `firmware/src`, Arduino,
  `wpa_supplicant` or `esp_wifi` sources. No splitflap submodule was required for
  the native build.
- `git check-ignore` confirms root/native cert directories, private key path,
  generated sdkconfig/build images and real legacy `secrets.h` are ignored.
  No real certificate, key or other credential was supplied or added.
- No automated runtime/fault/host parity test suite was added for the diagnostic
  scaffold. Error-path behavior is reviewed in source; NVS-full/version errors,
  allocation failure, event saturation and task stack margin remain untested at
  runtime.

Native GitHub Actions CI is provided separately from legacy CI. Remote run
status is reported in the draft PR; adding the workflow is not itself evidence
of a successful remote build.

## Remaining gates

Hardware identity/flash/wiring inventory; read-only offset backup/import and
partition migration; EAP-TLS trusted-time policy and negative certificate/name
tests; motor/serial protocol parity and timing; authenticated HTTP publication;
reviewed OTA partitioning, authentication and rollback. See
[migration audit](ESP_IDF_MIGRATION_AUDIT.md) and
[EAP-TLS plan](ESP_IDF_EAP_TLS_PLAN.md).
