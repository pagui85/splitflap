# U213 with Waveshare 7inch DSI LCD (E)

This optional v0.7.0 profile uses the 1280 × 800 Waveshare 7inch DSI LCD (E), SKU 29112, and the existing U213 OTA firmware. The owner verified U213 v1.3, 16 MiB flash, 32 MiB PSRAM at 20 MHz, USB Serial/JTAG, Ethernet, DHCP and reconnect with earlier firmware. The 7E, adapter, LCD power, touch, OTA on hardware and 80 MHz PSRAM remain bench tests.

**Waveshare classifies this exact panel as P4 rev v3.x-only.** Its Nano BSP rejects early revisions. This profile uses native ESP-IDF to test the unsupported U213 v1.3 combination. A successful build, I²C ACK or DMA frame counter does not establish panel compatibility. DSI, LCD I²C and LVGL stay inactive at boot. The experiment requires an explicit USB console command on each boot. An absent bridge fails at I²C probe before the D-PHY starts. The display task runs independently of Ethernet, recovery and OTA trial checks. See the [Waveshare Nano BSP compatibility matrix](https://components.espressif.com/components/waveshare/esp32_p4_nano/versions/3.0.0/readme).

## Adapter netlist

Use U213 mainboard **J6 LCD**. The second 24-pin connector is CSI and has a different pinout. The table is in electrical pin order. Confirm physical pin 1 and contact face on the actual assembly before committing the KiCad layout. Sources are [U213 mainboard schematic V0.2, sheet 2](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1217/SCH_StickPoE_SCH_Main_V0.2_20250829_2026_02_02_17_17_04.pdf) and the [Waveshare 7E interface definition](https://docs.waveshare.net/7inch_DSI_LCD_E/).

| U213 J6 | U213 schematic net | Waveshare FPC | Adapter connection |
|---:|---|---:|---|
| 1 | LCD_LEDK | — | Open, bare-panel boost return |
| 2 | LCD_LEDK | — | Open, bare-panel boost return |
| 3 | LCD_LEDA | — | Open, bare-panel boost output |
| 4 | LCD_LEDA | — | Open, bare-panel boost output |
| 5 | GND | 1, 4, 7, 10, 13 | Ground plane |
| 6 | SYS_3.3V | — | Open |
| 7 | TP_RST, GPIO3 | — | Open, 7E FPC has no reset |
| 8 | TP_INT, GPIO2 | — | Open, 7E FPC has no interrupt |
| 9 | SYS_SDA, GPIO0 | 12 | SDA0 |
| 10 | SYS_SCL, GPIO1 | 11 | SCL0 |
| 11 | GND | 1, 4, 7, 10, 13 | Ground plane |
| 12 | LCD_DSI_CK_N | 5 | Clock − |
| 13 | LCD_DSI_CK_P | 6 | Clock + |
| 14 | GND | 1, 4, 7, 10, 13 | Ground plane |
| 15 | LCD_DSI_D1_N | 2 | Data lane 1 − |
| 16 | LCD_DSI_D1_P | 3 | Data lane 1 + |
| 17 | GND | 1, 4, 7, 10, 13 | Ground plane |
| 18 | LCD_DSI_D0_N | 8 | Data lane 0 − |
| 19 | LCD_DSI_D0_P | 9 | Data lane 0 + |
| 20 | GND | 1, 4, 7, 10, 13 | Ground plane |
| 21 | LCD_RST, GPIO4 | — | Open, reset is via DCS |
| 22 | SYS_3.3V | — | Open |
| 23 | SYS_3.3V | — | Open |
| 24 | GND | 1, 4, 7, 10, 13 | Ground plane |
| Shell | GND | — | Ground any documented mating connector shield |
| — | — | 14, 15 | 3V3 direction undocumented. Isolate both pins. |

J6 uses dedicated D-PHY signals and system I²C GPIO0/1. GPIO33 controls the U213 bare-panel boost circuit and is left alone. The 7E has its own software backlight bridge. The underside GPIO13/12/11/10 (future MOSI/MISO/SCLK/latch) and GPIO9/8 (future SDA/SCL) remain available. Leave the camera connector and Chainlink carrier disconnected on the bench.

### Connector BOM and routing

| Side | Documented format | Candidate adapter receptacle |
|---|---|---|
| U213 DSI J6 | 24 positions, 0.50 mm | [Hirose FH12-24S-0.5SH(55)](https://www.hirose.com/product/p/CL0586-0521-0-55), bottom contact. FH12A-24S-0.5SH(55) is the top-contact option. |
| Waveshare 7E | 15 positions, 1.00 mm, supplied 50 mm and 160 mm opposite-face ribbons | [Amphenol SFW15R-1STE1LF](https://www.amphenol-cs.com/product/sfw15r1ste1lf.html), bottom contact. |

See the [M5Stack DSI accessory drawings](https://www.mouser.com/pdfDocs/AddOn_Display_Out_For_PoE-P4.pdf) and [7E package contents](https://www.waveshare.com/7inch-dsi-lcd-e.htm). The exact contact face on each owner board and the U213 cable face arrangement require visual and continuity checks. With everything unpowered, mark pin 1 on each footprint using the datasheet's viewing direction. Find the board pin-1 marks, inspect which face the contacts mate against and ohm the cables in their intended orientation. Record pin-1 and ground continuity. Verify flex thickness, stiffener length, insertion depth and latch clearance from the connector drawings. Give the ribbon slot a smooth edge, a gentle bend and strain relief clear of the latch.

- Route the three D-PHY pairs at 100 Ω differential on a fab-controlled stackup with continuous ground. Target ≤0.5 mm intra-pair skew on the adapter. Keep traces and cables short and use the shorter LCD ribbon where possible.
- Preserve polarity and lane order. Connect all ground pins, avoid stubs, plane splits and unnecessary vias, and add return vias near any layer changes. Preserve the D-PHY low-power DC path.
- Use the U213 system I²C pull-ups for the initial design. Confirm idle voltage and loading before adding pull-ups. Firmware uses 100 kHz.

### Power and 3.3 V hold point

Power the U213 over USB initially. Feed regulated, current-limited **5 V and GND** from a separate supply to the LCD's documented power input or matching pogo contacts. Verify polarity against the board markings. If using the 4-pin connector, use its documented 5 V/GND pair only. Give the LCD a dedicated return to its supply and tie both units to common signal ground. Keep the 5 V rails separate, the U213 LED boost pins open and all FPC 3.3 V pins isolated.

The public 7E pin table names pins 14 and 15 3V3 without specifying direction, current or powered-off behavior. Before mating any ribbon, check 14-to-15 continuity with power off. With the LCD powered by 5 V and the ribbon disconnected, measure pins 14/15 and SDA/SCL relative to LCD ground. A 3.3 V reading indicates a display-driven rail in that condition. A low reading leaves direction unresolved. If the bridge or touch fails with these pins isolated, obtain board-revision-specific supply and sequencing guidance from Waveshare before connecting a rail. Consider isolated test pads.

Mate and remove ribbons with both units off. Before operating with independent supplies, verify one powered unit does not raise the other's unpowered rails through I²C or other signals. If it does, stop and arrange coordinated power or powered-off signal isolation. For the first joint test after those checks, connect ground with power off and apply power to both units before starting firmware scanout. Measure inrush and maximum-brightness current before budgeting for shared power. M5Stack specifies a 6 W PoE output budget. See [Waveshare power examples](https://www.waveshare.com/wiki/7inch_DSI_LCD_(E)) and [U213 documentation](https://docs.m5stack.com/en/unit/Unit_PoE-P4).

## Driver and touch

| Setting | 7E configuration |
|---|---|
| DSI | P4 host 0, 2 lanes, requested 1250 Mb/s per lane, VC 0 |
| D-PHY | Internal LDO channel 3 at 2.5 V, as wired on U213 |
| DPI | 1280 × 800, 80 MHz, RGB565 |
| Horizontal back / sync / front | 156 / 40 / 20 pixels |
| Vertical back / sync / front | 48 / 40 / 40 lines |
| DSI reset and init | DCS 01 with no payload, wait 120 ms. 36 00, 11 00, wait 120 ms, 29 00, wait 20 ms, then start DPI. |
| I²C bridge | 7-bit 0x45, C0=01 C2=01 AC=01 AB=FF AA=01 AD=01, wait 1 s. Backlight register AB uses inverted PWM. Start at 40% after rendering. |
| Touch | BSP identifies GT911 at 7-bit 0x14. Probe 0x14 then 0x5D, verify product ID 911 at 0x8140, read native X/Y extents at 0x8048 and poll without reset or IRQ. One contact feeds LVGL. |
| Orientation | 0° or 180° landscape. Optional touch swap and mirror settings in menuconfig before applying rotation. |

The Waveshare 1.0.8 source matches the published DPI timings. The exact 7E BSP branch specifies 1250 Mb/s, while its generic default is 1500 Mb/s. The driver has no explicit DCS COLMOD command. This profile uses its 7E DPI macro through a C wrapper and reproduces its bridge/DCS sequence on J6 using native ESP-IDF. The vendor constructor configures legacy I²C on GPIO7/8, which would consume a carrier signal. The bridge sequence uses AB=FF in place of AB=00 to start with the backlight off. The native GT911 driver handles polling. Confirm coordinate limits and orientation on the actual screen. See the [panel component](https://components.espressif.com/components/waveshare/esp_lcd_dsi/versions/1.0.8/readme) and [7E BSP selection](https://github.com/waveshareteam/Waveshare-ESP32-components/blob/23a6012d5e87d27399cf90fb851cab3689e29159/bsp/esp32_p4_nano/include/bsp/display.h#L99).

M5Stack warns that SDK versions after 5.5.2 may flicker in its U213 example with an ST7123 1280 × 720 LCD. Comparing the 5.5.2 and 5.5.6 HAL shows revised PLL math and rounded host horizontal timing, including bridge front-porch compensation when the realized DPI clock differs from the request. Here the default 240 MHz DPI clock divides exactly by three for 80 MHz. The bridge compensation is zero for that case. Lane timing still uses the revised calculation. The early-revision build uses the SDK's legacy 20 MHz D-PHY PLL reference. Retain 5.5.6 and record flicker during the bench test. See the [M5Stack warning](https://docs.m5stack.com/en/esp_idf/unit_poe_p4/addon_display_in_for_poe-p4) and [ESP-IDF v5.5.6 HAL](https://github.com/espressif/esp-idf/blob/v5.5.6/components/hal/mipi_dsi_hal.c).

### Memory and display measurements

IDF allocates one aligned 2,048,000-byte RGB565 PSRAM framebuffer. LVGL renders into a 51,200-byte internal strip (20 lines). IDF copies strips and writes back the cache. DSI DMA reads the frame continuously. The single buffer can tear when updates overlap scanout. IDF reports DMA underruns over USB. The display command reports host DMA frame completions, flush errors, free heap and approximate aggregate CPU busy percentage. A PLL lock failure can leave the optional task waiting inside the SDK. The display profile uses a 1 ms FreeRTOS tick so the SDK's PLL wait yields to other tasks. Capture the last stage over USB or HTTP and reboot before trying again.

The 1496 × 928 totals yield a nominal 57.63 Hz. RGB565 scanout averages about 118 MB/s and needs 160 MB/s during active pixels. The 20 MHz DDR ×16 PSRAM raw ceiling is 80 MB/s. The optional profile selects the SDK's 80 MHz early-P4 HEX mode, with a 320 MB/s raw ceiling. Actual sustained bandwidth, CPU, heap, refresh, tearing and underruns require measurement. Compare idle UI, preview typing and Ethernet traffic. The headless build keeps the owner-tested 20 MHz setting. Espressif lists v3.0 as affected by the MSPI-750/751 overlap errata. See the [P4 errata](https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32p4/esp-chip-errata-en-master-esp32p4.pdf).

## Linux build, USB flash and OTA

Use [ESP-IDF v5.5.6 setup](ESP_IDF_P4_BRINGUP.md#identify-the-board-and-serial-port). The display lockfile pins the managed dependencies.

| Package | Version |
|---|---|
| ESP-IDF | 5.5.6, 880a203ae603d08a94d06aa01e0d6761958e5e9e |
| Waveshare esp_lcd_dsi | 1.0.8 |
| Espressif esp_lcd_touch_gt911 / esp_lcd_touch | 1.2.0 / 1.2.1 |
| LVGL | 9.3.0 |
| Espressif i2c_bus / cmake_utilities | 1.5.2 / 0.5.3 |

```bash
. "$HOME/esp/esp-idf-v5.5.6/export.sh"
cd firmware/esp-idf-bringup
BASE='sdkconfig.defaults;sdkconfig.p4-early;sdkconfig.p4-u213;sdkconfig.p4-u213-ota'
idf.py -B build-p4-u213-waveshare-7e -D IDF_TARGET=esp32p4 \
  -D SPLITFLAP_WAVESHARE_7E=ON \
  -D "SDKCONFIG_DEFAULTS=$BASE;sdkconfig.p4-u213-waveshare-7e" build
idf.py -B build-p4-u213-waveshare-7e size
python tests/check_isolation.py build-p4-u213-waveshare-7e p4-u213-waveshare-7e
```

Local verification used riscv32-esp-elf-g++ 14.2.0 (esp-14.2.0_20260121). Build isolation checks cover the display and LVGL, inactive Chainlink, the OTA table and embedded favicon. Factory is 3 MiB and each OTA slot is 4 MiB. The PR records build sizes and CI results.

Use the CI artifact named **splitflap-p4-u213-waveshare-7e-usb-serial-jtag** for a fresh install. Close any serial monitor. Run the commands below from the extracted package. For headless recovery use **splitflap-p4-u213-ota-usb-serial-jtag** with the same commands. A full USB package writes factory firmware and resets OTA boot selection. It preserves settings and the OTA identity with the existing Milestone 6 table. See [USB recovery and provisioning](ESP_IDF_U213_OTA.md#first-flash-and-usb-provisioning).

```bash
ls -l /dev/serial/by-id/ /dev/ttyACM*
PORT=/dev/ttyACM0
sha256sum -c SHA256SUMS
python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 chip_id
python -m esptool --chip esp32p4 --port "$PORT" --baud 115200 write_flash @flash_args
python -m serial.tools.miniterm "$PORT" 115200 --eol CR
```

For a provisioned U213 already running Milestone 6, use **splitflap-p4-u213-waveshare-7e-app-only**. Run the pinned HTTPS uploader from firmware/esp-idf-bringup. Reboot to leave scanout off before starting an OTA upload during initial bench testing. Use the IP from the serial net command and the saved certificate SHA-256. The uploader prompts for the USB-provisioned token and verifies exact-build trial confirmation after reboot. See [OTA acceptance and recovery](ESP_IDF_U213_OTA.md).

```bash
BOARD_IP=192.168.88.32
PIN=REPLACE_WITH_PROVISIONED_CERTIFICATE_SHA256
APP=/path/to/app-only/splitflap_p4_bringup.bin
python3 tools/u213_ota.py "$BOARD_IP" "$APP" --pin "$PIN"
```

## First bench session

1. With the display disconnected, boot v0.7.0. Run `info`, `pins`, `spi`, `net`, `ota` and `display`. Verify 32 MiB initialized PSRAM, 16 MiB flash, inactive Chainlink, DHCP and the OTA state. Display should report `off` and `manual_start_required`. Check `/health` and `/favicon.ico`. If using OTA, confirm the trial with the display disconnected.
2. Optionally run `display start experimental` with no panel and inspect `display`. Expect failure at `display_i2c` with Ethernet and USB still available. Reboot before another attempt.
3. With both units off, verify continuity, 3.3 V isolation, cable face, power polarity and common ground. Apply power after the checks above. Run `display start experimental`, then `display`. Allow several seconds for bridge init and rendering.
4. If the screen lights, verify branding, network state, uptime, OTA state, memory and 72-module capacity. Adjust brightness. Compare raw and mapped coordinates across the touch pad. Test preview text in three rows by 24 columns. Preview stays local and Chainlink remains inactive.
5. Save boot and display logs, `/health` with LCD off and streaming, heap and CPU readings, raw touch extents, supply current at several brightness levels, and video of any tearing or flicker. If the panel stays black, record the latest stage, measured power and bridge response. Use the headless USB package if recovery is needed.

Illustrative output after host initialization

```text
I (...) waveshare_7e: GT911 at 0x14, raw extent <X> x <Y>, polling
I (...) waveshare_7e: RGB565 1280x800, 2048000-byte PSRAM framebuffer, 51200-byte internal strip. Verify panel visually.
{"display_7e":"streaming_unverified","display_stage":"scanout","display_error":"ESP_OK",...}
internal_free=<measured bytes> external_free=<measured bytes>
```

Keep the future hardware default-off interlock in the motor power path for reset, startup and OTA. Leave motors, motor power, telemetry and high-side MOSFET circuitry disconnected during these tests.
