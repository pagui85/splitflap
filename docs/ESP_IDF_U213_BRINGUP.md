# M5Stack U213 Ethernet bring-up

This profile is for the M5Stack Unit PoE-P4 (U213) with early ESP32-P4 silicon,
including the owner's v1.3 unit. It adds Ethernet to the same diagnostic
console used in milestone 4. The board can be tested by itself. Leave the
carrier, Chainlink cables, motor supplies, display and other peripherals
disconnected for these tests.

## Verified board configuration

The V0.2 mainboard schematic, sheet 3, identifies U11 as IP101GRI. R47 pulls
the AD0 strap high and R46 pulls AD3 low. Those are the two address strap bits
on this package, giving PHY address 1. R42 pulls COL/RMII high. X3 supplies
the PHY's 25 MHz crystal reference. U11 pin 14 drives the resulting 50 MHz
clock, which the board routes to both U11 pin 10 and the MAC's GPIO50.
M5Stack's examples also use IP101, address 1 and an external clock from the
MAC's point of view. [M5Stack pin map and schematics][m5-board] ·
[Mainboard schematic][main] · [IP101GRI datasheet][ip101] ·
[M5Stack reference firmware][factory] · [M5Stack Ethernet example][example]

| Signal | ESP32-P4 GPIO | IP101GRI U11 pin | IDF configuration |
| --- | ---: | ---: | --- |
| MDC | 31 | 22 | SMI clock |
| MDIO | 52 | 23 | SMI data, R44 1.5 kΩ pull-up to 3.3 V |
| RESET_n | 51 | 32 | Active-low PHY reset |
| REF_CLK | 50 | 14 output, tied to 10 input | `EMAC_CLK_EXT_IN`, P4 clock output disabled |
| TX_EN | 49 | 5 | RMII TX enable |
| TXD0 | 34 | 9 | RMII TX bit 0 |
| TXD1 | 35 | 8 | RMII TX bit 1 |
| CRS_DV | 28 | 19 | RMII receive data valid |
| RXD0 | 29 | 18 | RMII RX bit 0 |
| RXD1 | 30 | 17 | RMII RX bit 1 |

The ESP-IDF v5.5.6 P4 RMII IO_MUX lists support all seven RMII data/clock
assignments. The shared driver validates them before touching GPIO. MDC,
MDIO and reset use GPIO assignments. GPIO34 and GPIO35 are P4 boot straps
that M5Stack also routes to the PHY; leave the ISP header's BOOT pin
disconnected during normal operation. The SDK's IP101 driver asserts reset
for 10 ms and waits 10 ms after release, exceeding the PHY's documented
2.5 ms post-reset interval. The shared MAC uses the SDK's default clock
divider and PHY autonegotiation. [P4 RMII mux source][idf-mux] ·
[ESP-IDF IP101 driver][idf-phy]

The overlay sets a 16 MiB DIO/40 MHz flash header and retains the 1 MiB
factory app partition. It uses the owner-tested 20 MHz P4 PSRAM probe with
capability allocation. The SDK detects the fitted 32 MiB in-package RAM at
boot and reports the external heap separately. Boot and application
allocations stay internal by default. The earlier 2 MiB-header warning
should disappear on the verified 16 MiB board. The generic P4 images
remain available as Ethernet-off recovery images.

## Flash on Linux

Use the download/OTG USB-C port by the RJ45 connector. The owner observed
`/dev/ttyACM0` via USB Serial/JTAG. Leave Ethernet unplugged for flashing.
Close any serial monitor first. Use the ESP-IDF v5.5.6 environment from the
[P4 setup guide](ESP_IDF_P4_BRINGUP.md#identify-the-board-and-serial-port),
which supplies esptool 4.13.0. The ROM checks are read-only.

```bash
. "$HOME/esp/esp-idf-v5.5.6/export.sh"
ls -l /dev/serial/by-id/ /dev/serial/by-path/
ls -l /dev/ttyACM*
PORT=/dev/ttyACM0
python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 chip_id
python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 flash_id
```

Confirm U213, early silicon and 16 MiB flash. The owner's unit reports
v1.3 and JEDEC `0x464018`. If automatic download entry fails, M5Stack
documents holding the side button until the internal green LED lights,
then releasing it and waiting for it to go out. Check the USB port again
after re-enumeration. Flashing replaces the bootloader, partition table and
factory app. Back up any existing flash content you need first. [M5Stack download mode][m5-board]

### Use the CI artifact

Download `splitflap-p4-u213-early-usb-serial-jtag` from the milestone 5
GitHub Actions run. Extract it, enter the extracted directory and run:

```bash
sha256sum -c SHA256SUMS
python -m esptool --chip esp32p4 image_info --version 2 splitflap_p4_bringup.bin
python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 write_flash @flash_args
python -m serial.tools.miniterm "$PORT" 115200 --eol CR
```

The generated `flash_args` sets DIO/40 MHz and 16 MB flash, with the
bootloader at `0x2000`, partition table at `0x8000`, and app at `0x10000`.
The app image should report revision support v0.0–v1.99. Exit miniterm
with Ctrl+]. Recheck the port path after a reset if the console disappears.

### Build from source

```bash
git clone --branch feature/esp-idf-u213 https://github.com/pagui85/splitflap.git "$HOME/splitflap-u213"
cd "$HOME/splitflap-u213/firmware/esp-idf-bringup"
BUILD=build-p4-u213-early
DEFAULTS='sdkconfig.defaults;sdkconfig.p4-early;sdkconfig.p4-u213'
idf.py -B "$BUILD" -D IDF_TARGET=esp32p4 -D "SDKCONFIG_DEFAULTS=$DEFAULTS" build
idf.py -B "$BUILD" size
python tests/check_isolation.py "$BUILD" p4-u213-early
idf.py -B "$BUILD" -p "$PORT" -b 115200 flash
idf.py -B "$BUILD" -p "$PORT" monitor
```

Use a fresh build directory for the overlay. Exit IDF Monitor with Ctrl+].
To recover from a startup problem, enter the ROM loader and reflash the
[generic early USB Serial/JTAG image](ESP_IDF_P4_BRINGUP.md#build-flash-and-monitor)
using its own flash arguments.

## USB, Ethernet and PoE test sequence

The U213 accepts 5 V USB-C or active PoE. The V0.2 power board routes the
RJ45's data-pair center taps and spare pairs through two bridge rectifiers to
the TPS23753A and isolated flyback supply. Its 5 V secondary `PSU_5V` reaches
`SYS_5V` through U3. The download USB-C connector's `USB_SUB` VBUS has a
separate path to `SYS_5V` through U1. The same drawing shows U4 (CH213K) with
`VI+` on `SYS_5V` and `VO-` on `USB_SUB`. The ideal diode orientation shows a potential path from
`SYS_5V` toward the download port VBUS. Use one supply at a time for
bring-up. Backfeed and simultaneous-source behavior remain unmeasured. Keep external 5 V and 3.3 V
disconnected from the underside headers. M5Stack rates the board for
IEEE 802.3at PoE and up to 6 W. [Power board schematic][power] ·
[M5Stack specifications][m5-board] · [TRENDnet switch specifications][switch]

1. Keep the U213 powered by its download USB-C cable. Connect RJ45 to an
   untagged access port on the TPE-TG50ES with PVID 30, DHCP available on
   VLAN 30, and PoE disabled for this port. The firmware sends untagged frames.
2. Run `info`, `pins`, `spi` and `net`. Save the console output.
   Confirm `startup=ESP_OK`, negotiated 10 or 100 Mb/s, a MAC address,
   a DHCP IPv4 address and gateway. Check DNS against the offered lease.
   Unset addresses appear as `0.0.0.0`.
3. From a host that can reach that VLAN, use the DHCP address.

   ```bash
   BOARD_IP=REPLACE_WITH_DHCP_IPV4
   curl --fail --show-error --max-time 5 "http://$BOARD_IP/health"
   ```

4. With USB power still connected, unplug RJ45 and wait for the link-down
   event. Run `net` and confirm `link=down` and `dhcp_ready=no`. Reconnect
   RJ45, wait for a lease, and run `net` and `curl` again. The lease may
   reuse the original address.
5. Disconnect USB and RJ45. Enable standard PoE+ on the VLAN 30 access
   port and connect only RJ45 to the U213. Verify the switch reports power
   delivery, then find the lease on the DHCP server by the recorded MAC
   address and repeat `curl`. For a PoE cold-reboot check, unplug and
   reconnect RJ45. Use the switch, DHCP server and HTTP for observation
   during this PoE-only test.

Expected excerpts below describe the new firmware. The IP, MAC, link speed
and available heap will come from the board and network.

```text
--- standalone ESP32-P4 bring-up info ---
board_profile=m5stack_u213_early
chip=ESP32-P4 model_id=<id> silicon_revision=1.3 raw_revision=103 cores=2
cpu_hz=360000000 status=ESP_OK
idf=v5.5.6 firmware=splitflap_p4_bringup/0.5.0
flash_physical_bytes=16777216 status=ESP_OK (JEDEC-based detection)
flash_image_access_limit_bytes=16777216 status=ESP_OK; configured_header=16MB dio/40m
flash_jedec_id=0x464018 status=ESP_OK
psram_detected_bytes=33554432 initialization=initialized heap_total_bytes=<bytes> heap_free_bytes=<bytes>
standalone=yes Chainlink_GPIO_activation=disabled (no HAL linked)
MOSI reported_assignment=-1 status=unassigned applied=no
Chainlink_SPI2_driver_linked=no initialized=no transfers=0 activation_command=none
ethernet=enabled startup=ESP_OK phy=IP101 address=1 rmii_clock=external_input dhcp=on http=/health
ethernet_pin_MDC=31
ethernet_pin_MDIO=52
ethernet_pin_RESET_n=51
ethernet_pin_REF_CLK_IN=50
ethernet_pin_REF_CLK_OUT=-1
p4-bringup> net
link=up speed_mbps=<10 or 100> mac=<MAC> dhcp_ready=yes
ip=<IPv4> gateway=<gateway> dns_primary=<DNS> dns_backup=<DNS or 0.0.0.0>
```

The HTTP response is JSON with `firmware`, `version`, `link`,
`dhcp_ready`, `speed_mbps`, `mac`, `ip`, `gateway` and `dns`.
If the link is up but DHCP stays pending, check the untagged VLAN/PVID and
the DHCP server. If startup fails before link negotiation, save the full
boot log and `net` output, including any PHY ID or MAC timeout.

## Future plug-in carrier and Chainlink

The mainboard schematic's underside SDIO J2 brings out all six proposed
signals. The adjacent ISP J3 provides ground at pin 1. Use connector pin
numbers from the schematic and verify mating orientation on the actual
unit before laying out the footprint. [Mainboard schematic, sheet 4][main] ·
[M5Stack underside pin map][m5-board]

| Proposed signal | GPIO | U213 underside pin | Destination and direction at U213 |
| --- | ---: | --- | --- |
| MOSI | 13 | SDIO J2-1 | Chainlink MOTOR_DATA, IDC pin 3, output |
| MISO | 12 | SDIO J2-2 | Chainlink SENSOR_DATA, IDC pin 5, input |
| SCLK | 11 | SDIO J2-3 | Chainlink CLOCK, IDC pin 1, output |
| LATCH | 10 | SDIO J2-4 | Chainlink LATCH, IDC pin 7, software GPIO output |
| I2C SDA | 9 | SDIO J2-5 | Carrier I2C, open drain |
| I2C SCL | 8 | SDIO J2-6 | Carrier I2C, open drain |
| GND | — | ISP J3-1 | Common logic ground, IDC pins 2/4/6/8 |

The pins are general-purpose 3.3 V signals in this board design, with no
overlap with the Ethernet or DSI/touch assignments. SPI2 uses the GPIO
matrix for the proposed MOSI, MISO and SCLK, and LATCH remains software
controlled. Check Chainlink modes, loopback and timing with a scope and
motor power disconnected before enabling the HAL. Verify 3.3 V logic
throughout the chain and size the pull-ups for the carrier I2C bus.
The board's 3.3 V and 5 V header rails require a separate current and
backfeed review before they power anything on the carrier. Keep 12 V
motor power on its own supply. [P4 SPI2 pin definitions][idf-spi] ·
[Existing Buddy and Driver audit](ESP_IDF_S3_HARDWARE.md)

The stock Chainlink IDC has no `OE_n` contact, and the Driver's 74HC595
output enables are tied low. The four SPI/latch signals suffice for the
stock protocol. An MCU pin on a carrier cannot disable those registers
through the stock cable. With all six underside GPIOs allocated, use an
independent, normally-off motor-supply interlock for a carrier that mates
through the underside headers alone. If a later design needs a dedicated
MCU control signal for that interlock, GPIO19 is a candidate on the
separate HAT interface. Trace `STD_GPIO19` from mainboard J5-10 through
HAT-board FPC J1-10 to HAT-board 2×8 J2-7 on the V0.2 schematic.
The product's Hat2-Bus illustration labels that position as pin 9
because it counts from the opposite end of the connector. Confirm the
orientation and continuity before assigning a carrier footprint.
Reserve GPIO19 provisionally only if the design also mates the HAT
interface. The interlock needs a hardware default-off state through
reset and power loss. This profile assigns no OE and enables no carrier
pins. [HAT expansion schematic][hat] ·
[Existing Buddy and Driver audit](ESP_IDF_S3_HARDWARE.md#driver-side-electrical-and-enable-audit)

Preserve the dedicated 24-pin DSI FPC and its GPIO4 reset, GPIO33
backlight and GPIO0–3 touch wiring. Keep clearance for the display
ribbon slot, connector access and bend beneath the carrier. [M5Stack LCD and touch pin map][m5-board]

## Verification status

| Check | Result |
| --- | --- |
| Local U213 build | ESP-IDF v5.5.6 at `880a203ae603d08a94d06aa01e0d6761958e5e9e`, RISC-V GCC 14.2.0 `esp-14.2.0_20260121` |
| App image | 537,712 bytes, 510,864 bytes free in the 1 MiB factory partition |
| Bootloader / partition table | 22,688 / 3,072 bytes |
| Image verification | App and bootloader checksums and validation hashes passed, 16 MiB header, v0.0–v1.99 |
| Isolation and reproducibility | PHY driver and HTTP linked, Chainlink SPI calls absent, all proposed pins unassigned, app/ELF/UTC hashes stable across `size` and a no-change build |
| Regression and host tests | Both host suites and all five prior standalone/compile profiles passed locally |
| Physical milestone 4 | Owner confirmed boot, memory, console, UTC provisioning and warm reboot on the U213 |
| Physical milestone 5 | U213 Ethernet image boot, DHCP, health, reconnect and PoE await owner testing. Chainlink electrical validation remains a later step |

[m5-board]: https://docs.m5stack.com/en/unit/Unit_PoE-P4
[main]: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1217/SCH_StickPoE_SCH_Main_V0.2_20250829_2026_02_02_17_17_04.pdf
[power]: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1217/SCH_StickPoE_SCH_POWER_V0.2_20250731_2026_02_02_17_17_39.pdf
[hat]: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1217/SCH_StickPoE_SCH_Hat_V0.2_20250801_2026_02_02_17_16_35.pdf
[ip101]: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1217/IP101GRI_Datasheet.pdf
[factory]: https://github.com/m5stack/M5Unit-PoE-P4-UserDemo/blob/2840dceb6d3c6d9e51b82fff3df4bb1bf22d463c/sdkconfig#L714
[example]: https://docs.m5stack.com/en/arduino/m5unit_poe-p4/ethernet
[idf-mux]: https://github.com/espressif/esp-idf/blob/v5.5.6/components/soc/esp32p4/emac_periph.c
[idf-phy]: https://github.com/espressif/esp-idf/blob/v5.5.6/components/esp_eth/src/phy/esp_eth_phy_ip101.c
[idf-spi]: https://github.com/espressif/esp-idf/blob/v5.5.6/components/soc/esp32p4/include/soc/spi_pins.h
[switch]: https://www.trendnet.com/langen/products/product-detail?prod=120_TPE-TG50ES
