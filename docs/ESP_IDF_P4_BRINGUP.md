# ESP32-P4 standalone diagnostics and Ethernet

The P4 uses the same native console project as the ESP32-S3 in PR #3. It boots
with USB power and an interactive serial console. All Chainlink assignments
default to unassigned, and the diagnostic image excludes the Chainlink HAL.
Ethernet and the HTTP server are off in the standalone profiles. The console
shares the S3 clock provenance rules. Build time and manual UTC are for
diagnostics, with no authorization for EAP-TLS.

## Identify the board and serial port

1. Record the maker, exact model, PCB revision, P4 and PHY markings, and USB
   connector labels. Find the schematic for that PCB revision. Use the board
   and one data-capable USB cable with the Buddy, harness, and motor power
   disconnected. Leave Ethernet unplugged for the first boot.
2. List ports before and after connecting the cable. A native USB Serial/JTAG
   port typically appears as `ttyACM*`, and a USB-to-UART bridge typically
   appears as `ttyUSB*`. Confirm the device using its label and USB ID.
3. Install the pinned SDK and run the read-only ROM commands below. Use the
   reported silicon revision to choose the image. Confirm the flash detection
   succeeds and reports at least 2 MiB. Save any existing firmware or data you
   need before flashing, since flashing replaces the bootloader, partition
   table and factory application on the selected board.

```bash
lsusb
ls -l /dev/serial/by-id/ /dev/serial/by-path/
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null

mkdir -p "$HOME/esp"
git clone --branch v5.5.6 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v5.5.6"
cd "$HOME/esp/esp-idf-v5.5.6"
test "$(git rev-parse HEAD)" = 880a203ae603d08a94d06aa01e0d6761958e5e9e
./install.sh esp32,esp32s3,esp32p4
. ./export.sh
idf.py --version
python -m esptool version

# Replace this with the port you identified above.
PORT=/dev/ttyACM0
udevadm info --query=property --name="$PORT"
python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 chip_id
python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 flash_id
```

The pinned tools are ESP-IDF v5.5.6, esptool v4.13.0 and Espressif GCC 14.2.0.
Install the usual ESP-IDF Linux prerequisites, including Git, Python 3, a host
compiler, CMake and Ninja. Use your distribution's serial device group if the
user cannot open the port. Common groups include `dialout` and `uucp`. Log out
and back in after a membership change. Close any other serial monitor before
using esptool.

## Build, flash and monitor

Clone the milestone branch once. In an existing clone, fetch and check out
`feature/esp-idf-p4` and enter `firmware/esp-idf-bringup`.

```bash
git clone --branch feature/esp-idf-p4 https://github.com/pagui85/splitflap.git "$HOME/splitflap-p4"
cd "$HOME/splitflap-p4/firmware/esp-idf-bringup"
```

| esptool silicon revision | Build directory | SDK defaults |
| --- | --- | --- |
| v3.0 through v3.99 | `build-p4-v3` | `sdkconfig.defaults` |
| v0.0 through v1.99 | `build-p4-early` | `sdkconfig.defaults;sdkconfig.p4-early` |

Use the matching pair in the shell. Stop if the reported revision falls
outside both ranges or the flash is smaller than 2 MiB. Keep the SDK's chip
revision checks enabled. Use a fresh directory for any new defaults so
existing `sdkconfig` values cannot override the profile.

```bash
# Select the pair matching the ROM output.
BUILD=build-p4-v3
DEFAULTS=sdkconfig.defaults
# For early silicon use these two assignments instead.
# BUILD=build-p4-early
# DEFAULTS='sdkconfig.defaults;sdkconfig.p4-early'

# If the board exposes a USB-to-UART0 bridge for console, use these two lines.
# BUILD="${BUILD}-uart0"
# DEFAULTS="${DEFAULTS};sdkconfig.p4-uart0"

idf.py -B "$BUILD" -D IDF_TARGET=esp32p4 -D "SDKCONFIG_DEFAULTS=$DEFAULTS" build
idf.py -B "$BUILD" size
python -m esptool --chip esp32p4 image_info --version 2 "$BUILD/splitflap_p4_bringup.bin"
idf.py -B "$BUILD" -p "$PORT" -b 115200 flash
idf.py -B "$BUILD" -p "$PORT" monitor
```

Use the port for the selected console when monitoring. If USB re-enumerates
during reset, check the path again. The default P4 image uses the native USB
Serial/JTAG console. The UART0 overlay uses the board's UART0 bridge at
115200 baud. Exit IDF Monitor with Ctrl+]. The P4 project's generated
`flash_args` places the bootloader at `0x2000`, partition table at `0x8000`
and application at `0x10000`. Let `idf.py flash` apply those offsets.

Both profiles use a DIO 40 MHz image with a 2 MiB flash header and 1 MiB
factory partition. `flash_physical_bytes` is the detected JEDEC capacity.
`flash_image_access_limit_bytes` is the capacity addressable by this image.
The optional 20 MHz in-package PSRAM probe uses the pinned SDK driver and
requires no configured capacity. When the SDK reports a missing chip or returns a probe error, the console
uses internal RAM. If the SDK halts on an unexpected memory ID or density,
use the PSRAM-off recovery step below. The SDK reports capacity in `psram_detected_bytes`, successful
initialization in `initialization`, and the usable external heap in
`heap_total_bytes` and `heap_free_bytes` on the PSRAM line. A zero result
with an initialization error calls for the boot log and board documentation.
This profile skips the SDK's boot memory test, which aborts on failure.
The diagnostics report memory discovery and heap availability.

### Flash a GitHub Actions build

The native ESP-IDF Actions run on PR #4 publishes `splitflap-p4-v3-usb-serial-jtag`
and `splitflap-p4-early-usb-serial-jtag`. Download and extract the package for the
revision reported by `chip_id`. Both packages use the native USB Serial/JTAG
console. Build from source for a UART0 console or verified Ethernet wiring.

From inside the extracted package, with the pinned SDK environment active:

```bash
sha256sum -c SHA256SUMS
python -m esptool --chip esp32p4 image_info --version 2 splitflap_p4_bringup.bin
python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 write_flash @flash_args
python -m serial.tools.miniterm "$PORT" 115200 --eol CR
```

Exit miniterm with Ctrl+]. Use the source-build route for IDF Monitor and symbol
decoding.

### Expected console output

This excerpt uses placeholders for board measurements.

```text
--- standalone ESP32-P4 bring-up info ---
chip=ESP32-P4 model_id=<id> silicon_revision=<major.minor> raw_revision=<revision> cores=<count>
cpu_hz=<frequency> status=ESP_OK
idf=v5.5.6 firmware=splitflap_p4_bringup/0.4.0
build_utc=<UTC> epoch=<epoch> source=<source> (approximate, untrusted)
reset_reason=<reason> code=<code>
flash_physical_bytes=<JEDEC size> status=ESP_OK (JEDEC-based detection)
psram_detected_bytes=<bytes> initialization=<state> heap_total_bytes=<bytes> heap_free_bytes=<bytes>
internal_8bit_total_bytes=<bytes> free_bytes=<bytes> largest_block_bytes=<bytes> minimum_free_bytes=<bytes>
time_trust=development_only EAP_TLS_authorized=no persistent=no
standalone=yes Chainlink_GPIO_activation=disabled (no HAL linked)
MOSI reported_assignment=-1 status=unassigned applied=no
Chainlink_SPI2_driver_linked=no initialized=no transfers=0 activation_command=none
ethernet=disabled board_profile=unset health_http=off
p4-bringup>
```

- [ ] Capture the ROM boot log and run `help`, `info`, `pins`, `spi`, and
      `net`. Save the full `info` output with the board model and revision.
- [ ] Run `time` and check that it reports development-only provenance.
      To exercise manual provisioning, use `time set YYYY-MM-DDTHH:MM:SSZ`.
- [ ] Run `reboot` and check that the prompt returns and manual time
      provisioning is cleared.
- [ ] If the app fails to boot, close the monitor and use the board's BOOT
      and RESET/EN controls to enter the ROM loader. On a board with both
      buttons, hold BOOT, tap RESET/EN, then release BOOT. Recheck the USB
      path, `chip_id` and `flash_id`, then reflash the matching standalone
      profile. Capture the exact error before retrying. Preserve the flash
      contents unless a deliberate full erase is required.
- [ ] If the boot log indicates a PSRAM-related startup problem, use a fresh
      recovery build directory with the same silicon and console defaults.
      Run `idf.py -B "$BUILD" -D IDF_TARGET=esp32p4 -D "SDKCONFIG_DEFAULTS=$DEFAULTS" menuconfig`
      after setting `BUILD` to that fresh directory. Disable PSRAM under
      component configuration, rebuild, and capture the serial result.

## Ethernet after the board schematic is verified

The component supports the P4 internal EMAC with an external RMII PHY. Confirm
the board actually uses that connection. A board with a SPI Ethernet controller,
an unsupported PHY or a different MAC interface requires its own driver work.

| Schematic item | Kconfig setting | What to record |
| --- | --- | --- |
| PHY part | `BRINGUP_ETH_PHY` | `1` LAN87xx, `2` RTL8201, `3` IP101, `4` KSZ80xx, `5` DP83848 |
| PHY address | `BRINGUP_ETH_PHY_ADDR` | Strap-selected address 0–31 |
| PHY reset | `BRINGUP_ETH_PHY_RESET` | Active-low GPIO or `-1` if reset is handled in hardware |
| Management | `BRINGUP_ETH_MDC`, `BRINGUP_ETH_MDIO` | Both P4 GPIOs and the board's MDIO pull-up |
| 50 MHz clock input | `BRINGUP_ETH_CLK_IN` | P4 pin receiving the reference clock |
| 50 MHz clock output | `BRINGUP_ETH_CLK_OUT` | `-1` for PHY/oscillator output, or the P4 output GPIO with a schematic trace looping back to the input |
| RMII TX | `BRINGUP_ETH_TX_EN`, `BRINGUP_ETH_TXD0`, `BRINGUP_ETH_TXD1` | All three transmit GPIOs |
| RMII RX | `BRINGUP_ETH_CRS_DV`, `BRINGUP_ETH_RXD0`, `BRINGUP_ETH_RXD1` | All three receive GPIOs |

Cross-check the reset pulse, PHY power, address straps, and clock direction
against the board and PHY documentation. Some P4 RMII mux choices also serve
as boot straps. Verify their reset levels on the schematic. Both USB pairs on GPIO24–27 and
UART0 37/38 are reserved by the diagnostic pin validator. Check the board
for additional pin uses. Incomplete assignments, duplicates or unsupported
RMII mux selections cause an error before the MAC or PHY driver touches GPIO.
The CI-only fixture in `tests/` uses synthetic pins. Build a separate local
profile for your board.

```bash
# Retain DEFAULTS for the detected silicon family and selected console.
BUILD="${BUILD}-verified-eth"
idf.py -B "$BUILD" -D IDF_TARGET=esp32p4 -D "SDKCONFIG_DEFAULTS=$DEFAULTS" menuconfig
# In P4 standalone Ethernet, enable Ethernet and enter the verified values.
idf.py -B "$BUILD" build
idf.py -B "$BUILD" -p "$PORT" -b 115200 flash
idf.py -B "$BUILD" -p "$PORT" monitor
```

- [ ] For the first Ethernet test, power the board from its documented USB
      port. Use an untagged access port in VLAN 30 on the TRENDnet TPE-TG50ES
      with PoE disabled on the test port. Confirm DHCP is available on VLAN 30.
      The app sends ordinary untagged Ethernet frames.
- [ ] Run `net` and capture the MAC, negotiated 10/100 Mb/s link, DHCP IPv4,
      gateway and primary/backup DNS. Unset addresses show as `0.0.0.0`.
      Confirm the lease on the router. The boot log reports link up followed by
      the DHCP address.
- [ ] From a host with access to the address, run
      `curl --fail --show-error http://BOARD_IPV4/health` after substituting
      the lease address. Expect JSON with `firmware`, `version`, `link`,
      `dhcp_ready`, `speed_mbps`, `mac`, `ip`, `gateway` and `dns`. The
      read-only endpoint is plain HTTP for the bring-up network.
- [ ] Unplug Ethernet while leaving USB power connected. Check `net` for
      `link=down` and a cleared lease. Reconnect, wait for link and DHCP, and
      repeat the HTTP request using the current lease address.
- [ ] For eventual PoE, verify the board's documented PoE powered-device
      circuit or a compatible active PoE splitter and its regulated output.
      Confirm the allowed power input and whether simultaneous USB power is
      supported before attaching both sources. An RJ45 connector alone gives
      no information about the board's PoE power path.

## Chainlink and calibration

The scaffold builds the existing SPI2 HAL for P4 and leaves it inactive.
The HAL rejects all P4 Chainlink GPIO assignments pending a verified board
profile. Diagnostic `pins` reports assignments only and detects conflicts
with USB Serial/JTAG, UART0, boot straps, configured Ethernet and duplicate
assignments. Before approving any pins, cross-check the exact board pinout,
flash and PSRAM routing, attached peripherals and boot behavior. Review the
Buddy header and logic levels, independent power and shared ground before a
harness, then scope reset and idle signals with the drivers disconnected.
Preserve the existing Chainlink Driver IDC wiring and the HAL's transfer
timing, modes, bit ordering and loopback semantics. Keep the working Buddy
controller on the current three modules for this milestone.

The legacy firmware in [configuration.cpp](../firmware/esp32/core/configuration.cpp)
writes version 1 nanopb `PB_PersistentConfiguration` to FFat `/config.pb`.
It checks the version and `num_flaps` on load. `module_offset_steps` (tag 3)
is an ordered array of `uint16_t` motor-step offsets, indexed by physical
channel. [Startup](../firmware/esp32/splitflap/main.cpp) copies the saved
prefix up to the configured channel count and zero-initializes the rest.
[Module code](../firmware/src/splitflap_module.h) retains offset adjustments
in steps modulo one drum revolution and rehomes when a restored offset changes.

Before replacing the Buddy controller, back up the original version 1 file
through a future export path and confirm flap count, steps per revolution,
and the channel-to-drum mapping across the three rows of 16. A future P4
motor application should import the same protobuf payload and preserve
those units, channel order and idle-only save behavior. The standalone
diagnostics have no calibration mount, import or writes.

## Build verification

These are local build results from the pinned v5.5.6 SDK using Xtensa
and RISC-V GCC 14.2.0 (`esp-14.2.0_20260121`), CMake 4.4.4, Ninja 1.13.2
and esptool 4.13.0. Every app fits the 1,048,576-byte factory partition.
Partition table binaries are 3,072 bytes.

| Project and profile | App bytes | Bootloader bytes | Result |
| --- | ---: | ---: | --- |
| Scaffold ESP32 | 156,848 | 26,000 | Pass |
| Scaffold ESP32-S3 | 183,408 | 20,464 | Pass |
| Scaffold ESP32-P4 v3.x | 193,840 | 23,328 | Pass |
| Standalone S3 UART0 | 290,000 | 20,464 | Pass |
| Standalone P4 v3.x USB Serial/JTAG | 290,784 | 23,424 | Pass |
| Standalone early P4 USB Serial/JTAG | 288,176 | 22,688 | Pass |
| P4 v3.x USB Serial/JTAG, Ethernet compile fixture | 541,168 | 23,424 | Pass |
| Early P4 UART0, Ethernet compile fixture | 564,384 | 22,592 | Pass |

From the repo root, the following host checks passed.

```bash
bash firmware/esp-idf/tests/run_host_tests.sh
bash firmware/esp-idf-bringup/tests/run_host_tests.sh
```

The host tests covered Chainlink golden vectors, loopback, the P4 GPIO guard,
ESP32/S3 fake-driver sequencing and cleanup, console parsing, clock provenance,
S3 PSRAM hints and build metadata. All five standalone builds passed linked
image isolation checks. App, ELF and generated metadata hashes were stable
across `idf.py size` and a no-change rebuild. Offline `image_info` verified
image checksums and the P4 revision ranges. The GitHub Actions matrix uses the
same SDK commit and includes all these profiles. It keeps the synthetic
Ethernet fixture in compile-only jobs.

Board boot, serial transport, flash and PSRAM behavior, PHY link, DHCP,
reconnect and HTTP still need the owner’s hardware validation. Keep the
Chainlink hardware disconnected until the exact board schematic and
voltage, power and signal assignments have been reviewed.
