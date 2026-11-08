# Owner verification gates for a future S3 bench session

This checklist prepares a later authorized hardware session. This milestone
does **not** authorize or perform flashing, motor power, calibration changes,
new PCB design or purchases. The current image does not start Chainlink I/O.

## Gate A — identify and inspect, disconnected

- [ ] Record the exact board manufacturer/model/revision, module shield marking
  and both sides' GPIO/power labels. Two USB-C ports alone do not identify a board.
- [ ] Match a manufacturer schematic and pinout to that exact revision; locate
  UART USB versus native USB, regulators, VBUS protection, boot/reset, RGB LED,
  PSRAM/flash reservations, and any factory peripherals.
- [ ] Confirm flash and PSRAM independently from markings/vendor data and later
  read-only chip/flash identification. Do not infer PSRAM from flash ID or treat
  the scaffold's 2 MiB header as a measurement. Read-only identification may
  reset the board; do it off the display harness in a separately approved session.
- [ ] Confirm the Buddy and Driver revisions and whether U2/C1/C2/barrel jack
  are fitted or bodged. Preserve the working T-Display, firmware and calibration.
- [ ] With all power/USB removed, continuity-check each U1 signal to the listed
  J1 pin, the J2 rails, all grounds, and U1-24 isolation. Identify the actual
  duplicate socket pads and connector key/pin1 view; never count by a photo guess.
- [ ] Verify no U1 GPIO27-to-OE route exists on the installed assembly. Stop on
  any difference from [the audited net table](ESP_IDF_S3_HARDWARE.md).

## Gate B — review the provisional harness before connecting it

- [ ] Replace each logical S3 GPIO name in the proposal with a verified physical
  header contact. Check GPIO10/11/12/13 are exposed and unused on this exact board.
- [ ] Remove the original T-Display entirely for replacement tests. Confirm no
  second controller or regulator remains connected to shared signals/rails.
- [ ] Keep original Driver boards, IDC connector, keying and daisy-chain wiring
  unchanged. Identify every +12V entry, including J2/J3/J7/remote Driver supply.
- [ ] Select one logic supply and record load/current margin and ground path.
  Verify 3.3 V at all logic nodes; no 5 V sensor-return path, parallel regulators,
  USB VBUS backfeed, powered-to-unpowered GPIO injection or alternate +12V path.
- [ ] Approve power sequencing, latch/clock/MOSI biases, cable length, strain
  relief and any required isolation/damping after reviewing actual circuitry.
- [ ] Produce/review an assembly-ready wiring drawing **only after** A and B are
  complete. The current logical table is deliberately insufficient for assembly.

## Gate C — later logic-only bench test, separate authorization required

- [ ] Physically disconnect all motor +12V sources; use no motor supply during
  initialization, reset, cleanup or error-path work. A software flag is not proof.
- [ ] Use a separate S3 test image/boot selection and preserve original flash
  and offsets. The diagnostic factory partition table is not an in-place upgrade.
- [ ] Explicitly configure/initialize the HAL in a later bench entry point; this
  PR has no GPIO activation option or motor command path in normal startup.
- [ ] Scope idle clock high/latch low, full-length zero TX and RX, MSB-first bit
  order, TX mode3 then RX mode2, latch high during RX, and absence of extra clocks.
  Check the first/last Driver and repeat under load; no compile-only timing claim.
- [ ] Check all loopbacks clear, set one bit, run at least two complete exchange
  cycles, validate only that echo, clear it and prove clear. Repeat every Driver.
  Add stuck/shorted/missing-chain cases; never accept floating MISO as a pass.
- [ ] Exercise bad pins/duplicate pins, missing chain, wrong capacity, allocation
  and driver failure, invalid coil bytes, owner-task misuse, repeated close and
  reset. Confirm clear error status and sensor-data invalidation behavior.
- [ ] Measure stack, DMA allocations, current, regulator temperature and waveform
  margin for the intended 48/72-module topology; reduce clock if measured needed.

Passing C establishes logic-interface evidence only. Motor-state-machine parity,
watchdog/fault behavior, calibration restore and authenticated publication still
need their own reviewed migration and future authorized motor-power session.
No RTC installation or EAP-TLS credentials are needed for A/B/C.
