# Milestone 2: Buddy audit and provisional S3 harness

**Not assembly-ready.** This is a source/net audit and a logical GPIO proposal,
not a confirmed pinout for the owner's two-USB-C board. No hardware was inspected,
flashed, powered or modified. Do not connect a harness using connector position
guesses. Driver PCBs and the existing keyed IDC chain stay unchanged.

## Evidence and limits

Audited at parent `b4cc6d6c2e6886e461486b0d5017bc923c2f22c5`:

- `electronics/chainlinkBuddyTDisplay/chainlinkBuddyTDisplay.sch`, matching
  `-cache.lib` symbol and `.kicad_pcb` pad/net assignments. The U1 footprint has
  repeated pads with the same number (alternate holes/pads), not additional GPIOs.
- `electronics/chainlinkDriver/chainlinkDriver.sch` and `.kicad_pcb` to verify
  supply rails, shift-register enables and buffered daisy-chain signals.
- `firmware/src/spi_io_config.h`, `firmware/src/config.h`,
  `firmware/esp32/core/splitflap_task.cpp` and `docs/ElectronicsGuide.md`.

The tables below use **KiCad U1 pad numbers**, not ESP module pin numbers or
physical left-to-right counting. NC means no connection to another Buddy circuit
in the schematic; the PCB may still assign an isolated `Net-(U1-Pad...)` to its
duplicate pads. Fitted components, board revisions, prior bodges and connector
orientation require owner continuity checks.

## Every T-Display-to-Buddy connection

| Buddy U1 pad | T-Display symbol name | Verified Buddy connection | Harness disposition |
| --- | --- | --- | --- |
| 1, 2, 10, 11, 14 | GND | Common ground, J1 even pins, J2-2, regulator/jack ground and H1..H4 pads | Common S3/logic-supply ground required |
| 3 | GPIO21 | NC | Leave open |
| 4 | GPIO22 | NC | Leave open |
| 5 | GPIO17 | NC | Leave open |
| 6 | GPIO2 | NC | Leave open |
| 7 | GPIO15 | NC | Leave open |
| 8 | GPIO13 | NC | Leave open; no Buddy NeoPixel connection |
| 9 | GPIO12 | NC | Leave open |
| 12 | 3V3 | +3V3 rail, J2-1 | Logic supply path; approval/budget gate below |
| 13 | 5V | +5V rail, optional U2 output and C2 | Leave open in provisional USB-powered S3 plan |
| 15 | GPIO27 | NC | **Not output enable** on this PCB; leave open |
| 16 | GPIO26 | NC | Leave open |
| 17 | GPIO25 | LATCH, J1-7 | S3 latch output proposal |
| 18 | GPIO33 | CLOCK, J1-1 | S3 SPI clock output proposal |
| 19 | GPIO32 | MOTOR_DATA, J1-3 | S3 MOSI output proposal |
| 20 | GPIO39 | SENSOR_DATA, J1-5 | S3 MISO input proposal |
| 21 | GPIO38 | NC | Leave open |
| 22 | GPIO37 | NC | Leave open |
| 23 | GPIO36 | NC | Leave open |
| 24 | 3V3 | **NC**, despite symbol/silkscreen power name | Do not use as the Buddy rail feed |

There is no Buddy connection for LCD SPI, USB D+/D-, UART TX/RX, reset/EN,
T-Display buttons or RTC I2C. The display, buttons, USB circuitry and 3.3 V
regulator belong to the original T-Display itself, not a native Buddy peripheral.

| Buddy connector/component | Verified source function |
| --- | --- |
| J1, keyed 2x4 2.54 mm IDC | 1 CLOCK, 3 MOTOR_DATA, 5 SENSOR_DATA, 7 LATCH; 2/4/6/8 GND. **No power or OE on IDC.** |
| J2, 3-position 5.08 mm screw terminal | 1 +3V3, 2 GND, 3 +12V. Logic and motor supply require separate rail connections beyond the IDC. |
| J3 barrel jack | Pin 1 +12V, pin 2 GND; switched contact pin 3 unused. Polarity symbol is provided, but verify the actual jack/adapter. |
| U2 LM7805_TO220 | Optional linear +12V-to-+5V regulator, pin 1 input, 2 GND, 3 output. It is **not** a 3.3 V regulator. |
| C1 / C2 | 10 uF from +12V to GND / 22 uF from +5V to GND; optional regulator support parts per assembly guide. |
| J7, four pads | **All four are +12V**, not a GPIO, UART or general-purpose header. Leave disconnected. |
| H1..H4 | Plated mounting pads tied to GND. Verify any conductive mounting arrangement. |

No fuse, reverse-polarity protection, logic level conversion, USB power mux or
independent motor-disable circuit is shown on the Buddy schematic. Their absence
from this drawing does not describe protection on a particular dev board or PSU.
Do not assume an LM7805 fitted by the owner has adequate heatsinking/current
margin; it dissipates approximately `(Vin - 5 V) * load current`.

## Driver-side electrical and enable audit

The audited Driver uses +3V3 for 74HC595 motor/LED registers, 74HC165 sensor
register and 74HC125 forwarding buffer. 74HC595 `/OE` (pin 13 on U1/U5/U7/U9)
is grounded; `/SRCLR` is tied high. Forwarding-buffer enables are grounded too.
The 74HC165 `/PL` is driven by LATCH; sensor bits occupy low six input bits and
loopbacks occupy bits 6/7. Clock, latch and motor serial output are forwarded
down the chain; sensor serial data returns from the chain.

The old `OUTPUT_ENABLE_PIN (27)` line is a **commented optional custom wiring
hook**, not evidence of routed enable on this Buddy/IDC. No OE harness wire is
proposed, and this milestone does not alter Driver wiring to create one. Holding
LATCH low does not clear the 595 output latch or turn off retained coil states.
The software's optional active-low OE can only hold outputs disabled if a future
separately reviewed circuit actually provides that signal; this API never
asserts it. Disconnect motor +12V physically for logic work.

The guide's Driver terminal label allows “Logic 3.3-5V”; that is **not permission
to connect a 5 V sensor return to an S3**. This proposal requires the complete
logic chain at measured 3.3 V. No level shifting is needed for verified 3.3 V
logic; 5 V logic requires a separate reviewed translation design and is outside
this harness proposal.

## Provisional logical harness map

These are GPIO **names** only. No S3 header position, wire color, top/bottom view
or soldering diagram is assigned until the exact board and both header pinouts
are confirmed. Remove the original T-Display before using its socket as an
adapter; two controllers must never drive the same signals or parallel regulators.

| Existing Buddy endpoint | Direction at S3 | Proposed S3 GPIO / endpoint | Status |
| --- | --- | --- | --- |
| U1-19 / J1-3 MOTOR_DATA | Output | GPIO11 (SPI2 MOSI / FSPID) | Source endpoint verified; S3 header exposure unconfirmed |
| U1-20 / J1-5 SENSOR_DATA | Input | GPIO13 (SPI2 MISO / FSPIQ) | Same |
| U1-18 / J1-1 CLOCK | Output | GPIO12 (SPI2 SCLK / FSPICLK) | Same |
| U1-17 / J1-7 LATCH | Output | GPIO10 (manual latch, **not automatic CS**) | Same |
| U1 GND / J2-2 / J1 even pins | Ground | Confirmed S3 GND, shared with chosen logic supply | Continuity and ground topology required |
| U1-12 / J2-1 +3V3 | Power | Exactly one approved 3.3 V logic source | Source and current budget unresolved; see below |
| U1-13 +5V; J2-3/J3/J7 +12V | None | **No connection to S3** | Leave out of harness |
| U1-15 GPIO27 / optional OE | None | No connection; `output_enable_n=-1` | No routed enable exists |

GPIO10..13 are SPI2-compatible reference pins in Espressif's DevKitC-1
[header tables](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html).
This does **not** identify the owner's USB-C board: that reference describes
Micro-USB hardware. The firmware supports GPIO-matrix routing through explicit
assignments and defaults to all pins unassigned.

Exclude S3 strapping GPIO0/3/45/46, USB GPIO19/20, potential flash/PSRAM GPIO26..37,
UART0 GPIO43/44, external JTAG GPIO39..42, and revision-dependent LED GPIO38/48.
The HAL conservatively allows only GPIO1/2/4..18/21 on S3 and still checks IDF's
valid input/output masks and duplicate assignments. A whitelist does not detect
a board-specific LED/peripheral or hidden connection; physical verification wins.
See Espressif's [GPIO restrictions](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/peripherals/gpio.html).

## Power, bias and protection gates

Preferred provisional arrangement: one confirmed USB connection powers the S3;
original T-Display is removed; Buddy +5V, all +12V feeds and J7 stay disconnected.
Use the S3's 3.3 V regulator output as the sole Buddy/Driver logic source **only
after** confirming its exposed output, available current/thermal budget and the
actual entire-chain logic load. This is not yet approved for 48/72 modules.

If that budget is insufficient, use a separately reviewed regulated 3.3 V logic
supply with common ground, **do not tie its output to the S3 regulator**, and
review signal sequencing/power-off isolation before connection. Independent
supplies can back-power either side through signal protection diodes. Never drive
the harness while one side's logic rail is off; a future buffer with specified
power-off isolation may be needed if sequencing cannot be guaranteed.

Never connect the Buddy +5V rail/LM7805 output to an S3 powered from USB, never
connect +12V to any S3 pin, and never parallel 3.3 V regulators. The reference
DevKitC guide treats USB, 5 V-pin power and 3.3 V-pin power as alternative supply
modes. Do not infer USB-C VBUS isolation or safe dual-USB operation for an unknown
board from that guide; start with one cable and inspect the exact board schematic.
The original T-Display battery connector must remain unused per the assembly guide.

The Buddy has no reset bias network for these four signals. Review external
LATCH pull-down and CLOCK pull-up (nominal idle high), plus MOSI pull-down, to the
**same** 3.3 V logic rail/ground before a future harness is finalized. Values and
existing biases must be checked on the actual boards; software initializes
levels before directions and does not rely on pulls. No pull on MISO substitutes
for connection/loopback tests. Keep the harness short and retain IDC ground
adjacency. Inspect edges/ringing at the first and last drivers before selecting
any source-series damping or lowering the configurable clock. No new passive,
level shifter, PCB or RTC purchase is required by this milestone.

## Firmware interface and timing contract

`firmware/esp-idf/components/chainlink_bus` compiles for both targets but is never
initialized by `app_main`. `Config` requires explicit pins, multiple-of-six total
chain capacity (6..252) and 0.1..4 MHz clock. ESP32 original pins remain
32/39/33/25; the S3 table above is a proposal, not an automatic profile.
`initialize(config, true)` is a caller assertion that motor supply is physically
disconnected, not a sensor or interlock. It validates first, sets optional OE
inactive, holds latch low, preloads clock high/MOSI low, allocates bounded
internal DMA buffers, and owns SPI2 exclusively in the calling task.

`exchange_logic` preserves forward buffer order and **MSB first**. It performs a
mode-3 TX of all output bytes followed by mode-2 RX with 30 ns input delay;
LATCH rises in the RX pre-callback and falls in the post-callback as in the old
firmware. No hardware CS, command, address, dummy phase, bit reversal or byte
reordering is added. `SPI_DEVICE_NO_DUMMY` explicitly prevents automatic extra
clocks. Logic-only validation rejects every motor-coil bit; only existing LED
and loopback bits can be set. This is not a motor-state-machine port.

One Driver consumes four output bytes/one input byte. Loopback output bits map
to forward bytes 2 bit7 / 1 bit3 for the nearest six-module group; input bits6/7
are paired per Driver. At least **two full exchange cycles** must occur between
setting a loopback bit and validating its echo, matching the old pipeline. No
automatic startup or ongoing loopback scheduler is started in this milestone.
Caller must check all-off, one-at-a-time isolation and return-to-off states.

SPI failures fault the interface, hold optional OE inactive and latch low, and
return the IDF error with stage logging; sensor output is copied only after a
successful pair of transfers. Treat sensors as invalid on **every** non-OK
return, including bad arguments. Cleanup removes devices, frees an owned bus
and DMA buffers, and returns errors; failed driver releases retain handles for
an explicit same-task retry. Destructor cleanup errors are fatal rather than
leaving callbacks referring to a destroyed object. Initialization, transfers,
cleanup and destruction must share one live owner task; bus sharing/concurrent
callers are unsupported. Cleanup emits no latch pulse and cannot guarantee
de-energized physical outputs. Runtime error paths and transfer waveforms still
need bench validation; see [owner checklist](ESP_IDF_S3_BENCH_CHECKLIST.md).
