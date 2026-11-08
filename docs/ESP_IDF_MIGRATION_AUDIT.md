# Native ESP-IDF migration audit

Audited 2026-11-08 from branch `feature/esp-idf-modern`, source commit
`a8a34c7ab1c9a4acfcc479990eff475af091b8f7`. The mission is in
[ESP_IDF_WORK_BRIEF.md](ESP_IDF_WORK_BRIEF.md). Existing code and Scott Bezek's
license/attribution are retained; new scaffold sources use Apache-2.0.

## Actual source versus brief

- `platformio.ini` pins `espressif32@3.4`, Arduino, `esp32dev`,
  `default_ffat.csv`, 230400 monitor baud; the old framework is the Arduino-ESP32
  1.0.6 generation. TFT_eSPI 2.4.25, PubSubClient 2.8, PacketSerial 1.4.0 and
  nanopb 0.4.6 are registry dependencies.
- **Current `[env:chainlink]` is `NUM_MODULES=6`, not 12.** The source default in
  `firmware/src/config.h` is 12 but is overridden by PlatformIO. MQTT and outbound
  HTTP are false; the T-Display is enabled. None of these settings were changed.
- `NUM_MODULES` is the capacity of the entire daisy chain served by one controller,
  not row width. `config.h` enforces multiples of six, one per Chainlink Driver's
  six outputs, with `NUM_LOOPBACKS=NUM_MODULES/3`. `spi_io_config.h` allocates
  four output bytes and one input byte per six modules and maps each motor,
  LED and loopback. 48 or 72 modules would imply 8 or 12 drivers **if** a single
  Buddy controls the whole display. Confirm controller topology first.
- Generated protobuf arrays allow 255 modules; queue copies, state snapshots,
  task stacks, loopback arrays and SPI lengths must still be budgeted at 48/72.
  Compile-time capacity alone does not prove real-time performance.

## Port boundaries and preserved behavior

| Source | Dependency / obstacle | Incremental port and acceptance gate |
| --- | --- | --- |
| `firmware/esp32/splitflap/main.cpp` | Arduino `setup`, global constructors, `loopTask` deletion; motor task on core 1, serial/display/network on core 0 | Native `app_main` exists separately. Initialize configuration before the future motor task; preserve task ownership and measure affinity/priority under network load. |
| `firmware/esp32/core/task.h`, guards | FreeRTOS abstractions import `Arduino.h`; task stack sizes already use IDF byte semantics | Replace includes with IDF headers and explicit lifecycle/error handling when these classes are ported. Do not copy constructor-time allocations blindly. |
| `firmware/src/splitflap_module.h`, `firmware/src/acceleration.h` | `micros`, Arduino types/helpers, unsigned wraparound, acceleration/state/sensor handling | Extract clock and I/O boundaries; retain state/step math. Compare recorded deterministic traces for homing, missed/unexpected sensors, offsets, panic and timer wrap before hardware trials. |
| `firmware/src/spi_io_config.h` | Old IDF SPI structs/host constants plus Arduino GPIO; DMA/alignment; two SPI modes and latch callbacks | Native `esp_driver_spi`/GPIO adapter; retain bit ordering, loopback checks, latch timing and input delay. Verify transfers and jitter with a logic analyzer. |
| `firmware/esp32/core/splitflap_task.cpp` | Busy update loop, watchdog registration/reset, 5-entry command queue, state/config mutexes; startup and continuous loopback enforcement | Retain single ownership of motor state. Current loop does not sleep while stepping; do not replace with health-task tick timing. Bounded command admission/backpressure and explicit fault disable paths need tests. |
| `firmware/esp32/core/configuration.*` | FFat, `/config.pb`, nanopb `PB_PersistentConfiguration`, version 1, `num_flaps` validation and offsets; `FFat.begin(true)` can format on failure | Preserve original bytes/offset units and ordering. Start with read-only import and a backup; never silently format. Choose FAT compatibility or versioned NVS conversion only after real partition inventory and restore tests. Scaffold neither reads nor rewrites calibration. |
| `firmware/esp32/core/uart_stream.*` | Arduino `Stream` over IDF UART, old `use_ref_tick` field, UART0 buffers | Native UART adapter; preserve baud/protocol selection. Keep native logs off the binary protocol stream once it is ported. |
| `firmware/esp32/splitflap/serial_*` + `proto_gen` + `proto/splitflap.proto` | JSON protocol switch; PacketSerial COBS; protobuf plus little-endian CRC32, nonce ACK/dedup and periodic states | Keep schema and checked-in generated code unchanged. Pin matching nanopb runtime; test captured frames, CRC/endian, truncation, retries, invalid indexes, protocol negotiation and state pacing. |
| `firmware/esp32/splitflap/display_task.*`, `display_layouts.h` | Arduino TFT_eSPI/ST7789, fonts, `String`; shares state and uses other SPI host | Port later with `esp_lcd` or a reviewed native adapter. Preserve state snapshots; budget fonts, refresh, bus ownership. |
| `firmware/esp32/splitflap/mqtt_task.*` | WiFi/ArduinoOTA/PubSubClient, `String`, local `secrets.h`; network reconnect logic | Replace separately after authenticated connectivity. Queue validated commands into motor owner; no direct motor access from network callbacks. |
| `firmware/esp32/splitflap/http_task.*` | Arduino HTTPClient, SNTP/json11, weather polling, WiFi credentials | This is an **outbound weather client**, not a server. Do not describe it as `/api/publish`. Implement the requested native server in a later component with a bounded JSON contract and access control. |
| `firmware/esp32/base`, `tester` | MCP23017, INA219, FastLED, Firestore/tester-specific behavior | Explicitly outside first Buddy port; keep source filters separate. |
| build metadata + legacy CI | Python prebuild macros and old Arduino package dependencies; `pio.yml` uses ubuntu-20.04 and several stale environment names | Keep legacy files unchanged. Native CI is independent. Report legacy build evidence/limitations separately rather than interpreting native build success as regression coverage. |

Other audit follow-ups: `SerialTask::log` allocates a string before a nonblocking
queue send without freeing it on a full queue; native ports should fix that
ownership issue explicitly. `setup()` starts the motor task before assigning
its configuration/restoring offsets; native startup should remove that race.
Neither finding is changed in the preserved legacy application in this PR.

## Hardware evidence and open questions

Source references: [ElectronicsGuide.md](ElectronicsGuide.md),
[v2/Assembly.md](v2/Assembly.md),
`electronics/chainlinkBuddyTDisplay/chainlinkBuddyTDisplay.sch`,
`firmware/src/spi_io_config.h`, `firmware/src/config.h`, `platformio.ini`.
The schematic's module symbol is `ESP32Modules:TTGO_TDisplay`; it does not
identify the actual owner's module marking or measured flash capacity.

| Item | Source evidence | Still to verify on owner's hardware |
| --- | --- | --- |
| CPU | Original ESP32 dual-core/T-Display design, esp32dev legacy target | Exact module marking, chip variant/revision, PSRAM; do not assume a newer T-Display S3 pinout |
| Flash/partitions | Legacy selects framework `default_ffat.csv`; no owner flash dump supplied | Real capacity and current table, FFat location and config backup; OTA slot budget |
| Motor bus | HSPI, MOSI GPIO32, MISO GPIO39, clock GPIO33, latch GPIO25, 4 MHz; TX mode 3, RX mode 2, 30 ns input delay | Wiring/revision, electrical levels, cable length, latch waveform, output-enable wiring and safe power-up state |
| Output enable | GPIO27 optional in source and commented out; may be hardwired | Whether outputs can actually be held disabled in reset/fault; a GPIO-free image cannot guarantee de-energized coils |
| LCD | ST7789 135x240, SPI MOSI19/clock18/CS5/DC16/reset23/backlight4, 40 MHz, VSPI | Installed display orientation/revision and conflicts; scaffold does not initialize these pins |
| Chain capacity | Six modules/driver, two loopbacks/driver; multiple-of-six validation | Number of controllers/drivers and 48→72 layout; supply margin and worst-case motion load |
| NeoPixel/console | GPIO13 default; legacy console/UART0 230400 | Actual fitted peripherals and protocol-host expectations |

The downloaded Arduino framework package `3.10006.210326` defines
`default_ffat.csv` with NVS at `0x9000`/`0x5000`, OTA data at
`0xe000`/`0x2000`, two `0x140000` app slots at `0x10000` and `0x150000`,
and FFat at `0x290000`/`0x170000` (a 4 MiB layout). This verifies the package
default, **not** what is currently installed on the owner's device. The native
factory scaffold also changes the NVS extent; it must not be flashed over that
layout as a storage-preserving upgrade.

## Roadmap with gates

1. **This PR:** isolated native build, non-erasing NVS startup, task/event/log
   scaffold, source audit and exact build report. No behavioral parity claim.
2. **EAP-TLS:** follow [the security plan](ESP_IDF_EAP_TLS_PLAN.md). Prove CA,
   expected RADIUS identity and validity-time rejection before enabling networking;
   per-device provisioning and bounded reconnect/DHCP behavior are prerequisites.
3. **Control/serial/storage:** separate adapters from unchanged state math;
   build host trace/frame tests, prove offset import/recovery, then owner-run
   hardware timing and fault tests with a reviewed power/enable plan.
4. **HTTP server:** specify `POST /api/publish`, limits, auth, HTTP error codes,
   fixed-capacity queue handoff and concurrency tests before accepting commands.
5. **OTA:** after chip/flash confirmation, allocate slots, validate/authenticate
   images, implement rollback and recovery; evaluate secure boot/signed images
   and key lifecycle for the actual silicon. Never inherit ArduinoOTA's sample
   `replace_me!` password as an authentication design.
