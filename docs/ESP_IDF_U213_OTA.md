# U213 Ethernet OTA and persistent settings

Use this profile on the early-silicon M5Stack Unit PoE-P4 (U213) with 16 MiB
flash. It preserves the Ethernet wiring and 20 MHz PSRAM probe tested in
milestone 5. Read the [U213 Ethernet and power guide](ESP_IDF_U213_BRINGUP.md)
before connecting power. All bench steps here use the board alone.

## Flash layout

| Partition | Offset | Size | Purpose |
| --- | ---: | ---: | --- |
| NVS | `0x9000` | 20 KiB | ESP-IDF default NVS |
| OTA metadata | `0xe000` | 8 KiB | Two boot selection sectors |
| Factory app | `0x10000` | 3 MiB | USB-installed recovery and first OTA source |
| OTA 0 | `0x310000` | 4 MiB | Application slot |
| OTA 1 | `0x710000` | 4 MiB | Application slot |
| Splitflap settings | `0xb10000` | 64 KiB | Versioned settings and update journal |
| OTA identity | `0xb20000` | 64 KiB | Device key, certificate and token verifier |

The table leaves 4,928 KiB unallocated through the end of the 16 MiB flash.
OTA writes only the inactive application slot and, after validation, updates
boot selection. The USB package installs the bootloader at `0x2000`, table
at `0x8000`, factory app at `0x10000` and blank OTA metadata at `0xe000`.
The USB package leaves the settings and identity partitions intact. Preserve
anything needed from a previous flash layout before installing this layout.

## First flash and USB provisioning

1. Download and extract `splitflap-p4-u213-ota-usb-serial-jtag` from the PR's
   Native ESP-IDF Actions run. Use the ESP-IDF v5.5.6 shell in the
   [Linux setup guide](ESP_IDF_P4_BRINGUP.md#identify-the-board-and-serial-port).
   Keep Ethernet and carrier wiring disconnected. Close any existing monitor.
2. Confirm the download port, U213 chip and flash using read-only ROM checks,
   then flash from inside the extracted package.

   ```bash
   . "$HOME/esp/esp-idf-v5.5.6/export.sh"
   ls -l /dev/serial/by-id/ /dev/ttyACM*
   PORT=/dev/ttyACM0
   python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 chip_id
   python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 flash_id
   sha256sum -c SHA256SUMS
   python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 write_flash @flash_args
   python -m serial.tools.miniterm "$PORT" 115200 --eol CR
   ```

3. Run `info`, `pins`, `spi`, `ota` and `config`. Expect firmware v0.6.0,
   a 16 MiB header, initialized 32 MiB PSRAM, a `factory` running
   partition, `ota_provisioned=false`, and inactive Chainlink GPIO and SPI.
4. Run `ota provision` once on the private USB console. The board creates
   an ECDSA P-256 identity and a random 256-bit upload token. Save
   `ota_certificate_sha256` and `ota_token` from the one-time output in a
   password manager. Avoid terminal logging or sharing the provisioning
   output. Run `reboot`, then `ota` and expect `ota_provisioned=true`.
   No OTA listener starts until provisioning has been saved and the device
   reboots.
5. For recovery of a lost identity, use USB with Ethernet disconnected.
   Erase only the dedicated 64 KiB credential partition under the installed
   OTA table, then reboot and provision again. Replace the saved token and
   fingerprint together.

   ```bash
   python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 erase_region 0xb20000 0x10000
   ```

If the serial port does not reappear automatically, close the monitor,
enter download mode using the U213 side button, then check the device path.
Reflash the full OTA USB package to restore the factory app and clear the
OTA boot selection. This recovery also works if both app slots become
unusable. It preserves the settings and identity addresses above.

### Build the same images locally

```bash
git clone --branch feature/esp-idf-u213-ota https://github.com/pagui85/splitflap.git "$HOME/splitflap-u213-ota"
cd "$HOME/splitflap-u213-ota/firmware/esp-idf-bringup"
DEFAULTS='sdkconfig.defaults;sdkconfig.p4-early;sdkconfig.p4-u213;sdkconfig.p4-u213-ota'
idf.py -B build-p4-u213-ota -D IDF_TARGET=esp32p4 -D "SDKCONFIG_DEFAULTS=$DEFAULTS" build
idf.py -B build-p4-u213-ota size
idf.py -B build-p4-u213-ota-fail -D IDF_TARGET=esp32p4 \
  -D "SDKCONFIG_DEFAULTS=$DEFAULTS;tests/sdkconfig.p4-u213-ota-fail" build
```

Use separate build directories and keep the rollback-test image for OTA
testing only. Local USB flashing uses
`idf.py -B build-p4-u213-ota -p "$PORT" -b 115200 flash monitor`.

## Transport and trial boot

Read-only HTTP `GET /health` remains available on port 80. Firmware upload
and trial confirmation are HTTPS-only on port 443. The Linux utility pins
the exact SHA-256 of the server certificate obtained over USB. It checks
the pin on each TLS connection before sending any HTTP data. The random
bearer token is prompted with hidden input and sent only inside that
connection. No CA lookup, DNS name or device wall clock is required for
the pinned identity. Keep the token and fingerprint from a trusted USB
session.

The device stores a SHA-256 token verifier and its TLS private key in
dedicated NVS. Secure boot, flash encryption and signed release verification
are outside this development profile. Someone with physical flash access
can read the device key or reprogram the board. A holder of the upload
token can install a compatible image. The OTA image hash detects corruption.
Use the development VLAN or a similarly restricted network and keep the
upload port off the public internet.

Upload checks run before changing boot selection. They enforce authorization,
content length and slot capacity, the early P4 chip revision range, a 16 MiB
flash header, the Splitflap U213 OTA descriptor, the upload SHA-256 and IDF's
image validation. App-only binaries are required. A truncated, interrupted
or rejected transfer leaves the current app running. A completed transfer
returns HTTP 202 before the board reboots.

A trial boot has 90 seconds to pass both checks.

1. USB console starts, the versioned settings and OTA identity can be read,
   and Ethernet, read-only HTTP and HTTPS service initialization succeed.
2. The Linux uploader reaches `/health` over pinned HTTPS after DHCP,
   sees the expected build and running slot in `pending_verify`, then sends
   authenticated confirmation for that exact build. It waits for `valid`
   and `confirmed` before reporting success.

Startup failure triggers rollback immediately. Reset, power loss or trial
timeout before confirmation returns to the previously valid app, with the
factory image available as the first fallback. The same DHCP address must
remain reachable by the uploader during the trial. Use a DHCP reservation
if the address changes across boots. The upload journal reports the latest
authenticated attempt that passed the request framing and length checks.
It records interruptions, image rejection, write failures, staging,
confirmation and rollback.

`/health` includes `version`, `build_id` (full app ELF SHA-256), `uptime_s`,
`running_partition`, `ota_image_state`, `last_update_result`,
`ota_provisioned` and `ota_trial_ready` alongside the network fields.

## Owner validation checklist

Use USB power and a data-only Ethernet connection for the first test, with
PoE off on the switch port. Test the existing unmanaged network or the
untagged IoT VLAN 30 when available. Use the IP reported by `net`. The
examples use the address observed during milestone 5.

```bash
BOARD_IP=192.168.88.32
PIN=REPLACE_WITH_OTA_CERTIFICATE_SHA256
UPLOADER=tools/u213_ota.py
APP=/path/to/app-only/splitflap_p4_bringup.bin
FAIL_APP=/path/to/rollback-test/splitflap_p4_bringup.bin
curl --fail --show-error "http://$BOARD_IP/health"
```

Run the following from `firmware/esp-idf-bringup` in the checkout. The
uploader uses Python 3 and its standard library. Obtain the two app-only
files from the PR's normal and rollback-test artifacts, or use the local
builds above. Never send the full USB package or its bootloader.

| Test | Procedure and expected result |
| --- | --- |
| Persistent marker | Over USB run `config probe 6272`. `config` reports version 1, marker 6272 and zero calibrated channels. |
| Invalid image | Run `python3 "$UPLOADER" "$BOARD_IP" "$APP" --pin "$PIN" --test-corrupt`. It corrupts the embedded digest in memory and verifies HTTP 422, the rejection journal and the original app still running. |
| Interrupted upload | Run `python3 "$UPLOADER" "$BOARD_IP" "$APP" --pin "$PIN" --interrupt-after 65536`. The client disconnects without completing the body. Verify the previous build and `last_update_result=interrupted` after the receive timeout. |
| Successful upload | Run `python3 "$UPLOADER" "$BOARD_IP" "$APP" --pin "$PIN"`. From factory, the first update should select `ota_0`, reconnect and confirm. A repeat update exercises `ota_1`. |
| Rollback | Run `python3 "$UPLOADER" "$BOARD_IP" "$FAIL_APP" --pin "$PIN" --expect-rollback`. The test app deliberately fails its startup check. Verify the previous build, partition and `rolled_back` result. |
| Retention | Use USB `config` after a valid update and after rollback. The marker stays 6272. The same saved token and pin continue to work. |
| USB recovery | Reflash the complete USB package and verify `running_partition=factory`. The marker and identity remain readable. |

### Verify the public favicon

Run this from `firmware/esp-idf-bringup` after DHCP. The check makes an
unauthenticated HTTP request, checks the status and media type, and
compares the payload to the original ICO embedded in the firmware.

```bash
(
  set -e
  ICON_DIR=$(mktemp -d)
  trap 'rm -f "$ICON_DIR/favicon.ico"; rmdir "$ICON_DIR"' EXIT
  response=$(curl --fail --silent --show-error --max-time 5 \
    --output "$ICON_DIR/favicon.ico" --write-out '%{http_code} %{content_type}' \
    "http://$BOARD_IP/favicon.ico")
  test "$response" = '200 image/x-icon'
  cmp "$ICON_DIR/favicon.ico" components/bringup_ethernet/assets/favicon.ico
  echo 'Favicon response verified'
)
```

For an authentication check, run the upload with an incorrect token and
verify that the update journal and running build stay unchanged. For a
trial reachability check, disconnect Ethernet after staging a valid app,
wait for the trial timeout, then reconnect and verify rollback.

## Calibration and motor power

The version 1 settings record uses little-endian fields. It stores a
32-bit schema version, a 32-bit bench marker, a 16-bit flap count, a 16-bit
channel count and 72 ordered 16-bit motor-step offsets. Empty calibration
has zero channels. Unrecognized versions and invalid counts fail closed
and are preserved. Only the bench marker is writable through this milestone's
console. It leaves the remaining record bytes unchanged.

A future importer can read the Buddy's version 1 nanopb `/config.pb`,
validate flap count and channel order, and copy the ordered step offsets
into this record. Preserve the same motor-step units and physical channel
mapping. A legacy file with more than 72 offsets needs explicit mapping
before import. This milestone leaves the legacy firmware and calibration
storage in place.

The U213 remains independently powered. Before any motor firmware is
enabled, use a hardware default-off interlock on the separately switched
and fused 12 V Mean Well banks. Keep motor power inhibited during upload,
trial, rollback, reset and startup until the motor application has
established safe outputs and explicitly enabled power. The stock IDC
provides no output enable. Power telemetry and integrated high-side
switch circuitry remain part of the future power PCB design.

## Build verification

Local builds used ESP-IDF v5.5.6, Espressif RISC-V GCC 14.2.0
(`esp-14.2.0_20260121`) and esptool 4.13.0.

| Image | App bytes | Bootloader bytes | Partition table bytes |
| --- | ---: | ---: | ---: |
| U213 OTA v0.6.0 | 796,576 | 22,816 | 3,072 |
| U213 rollback test | 796,432 | 22,816 | 3,072 |
| U213 Ethernet-only regression build | 540,208 | 22,688 | 3,072 |

Both OTA images include the original 15,086-byte ICO and use an
8,192-byte blank OTA metadata image for USB setup. The build checks
verified the partition table and Chainlink isolation. Offline image
validation confirmed the 16 MiB flash header, early-silicon revision
range and image hashes. No-change builds preserved app, ELF and generated
metadata hashes. Host suites covered the existing diagnostics and the
uploader's pin mismatch, trial confirmation and rollback verification.
Physical OTA, credential provisioning and HTTP favicon checks await
the owner's bench test.

[ESP-IDF v5.5.6 OTA reference](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32p4/api-reference/system/ota.html)
