# ChatGPT Work assignment — Native ESP-IDF modernization

Repository: https://github.com/pagui85/splitflap
Development branch: `feature/esp-idf-modern`
Base: `master` (keep untouched)

## Mission
Modernize Scott Bezek's split-flap / Chainlink Buddy ESP32 firmware from the old PlatformIO espressif32@3.4 / Arduino-ESP32 1.0.6 stack to an actively supported **native ESP-IDF, C++ and FreeRTOS** architecture using Espressif's official `idf.py`/CMake tooling. Preserve original motor-control behavior, serial/protobuf messages, sensor behavior, calibration, and persistent offsets. Do **not** rewrite everything at once.

**First deliverable:** inspect repo, document migration obstacles, create an isolated native ESP-IDF application skeleton, verify a clean build using a documented toolchain, and open a **draft PR** targeting `pagui85/splitflap:master`. Include an honest status report and incremental roadmap. Do not merge or flash hardware.

## Known source layout
- `firmware/esp32/splitflap/main.cpp`: Arduino entry point; instantiates `SplitflapTask`, `SerialTask`, optional `DisplayTask`, `MQTTTask`, `HTTPTask`; starts FreeRTOS tasks and deletes Arduino loopTask.
- Reusable logic in `firmware/esp32/core`, `firmware/esp32/splitflap`, `firmware/esp32/proto_gen`.
- `[env:chainlink]` in `platformio.ini` uses `-DCHAINLINK -DNUM_MODULES=12`; physical display has four 12-module rows (48 total) with plans for four 18-module rows (72). Verify semantics of `NUM_MODULES` rather than changing it blindly.
- Existing `http_task.cpp` makes outbound weather API requests; **it is not an HTTP server**. MQTT/HTTP tasks may be disabled in default Chainlink flags.
- Board configured as `esp32dev` in legacy PlatformIO; **verify actual Chainlink Buddy module/chip, flash capacity, partition constraints, pins and wiring** from source/docs, and flag remaining hardware questions.

## Target architecture and next milestones
1. Native ESP-IDF CMake project in an **isolated folder**, e.g. `firmware/esp-idf/`. Keep old PlatformIO build intact. Modular components with `app_main`, ESP logging, NVS init, FreeRTOS tasks/events. Choose a supported stable ESP-IDF release, document why, build `idf.py set-target esp32 && idf.py build` (or verified target as appropriate). Add CI if practical.
2. **WPA2-Enterprise EAP-TLS** client for SSID `paguiEAP`; client identity `splitflap01.lab.danpapuga.com`; FreeIPA-issued EC P-256 client cert + key, FreeIPA CA trust anchor, expected RADIUS server name `radius01.lab.danpapuga.com`. Native supported ESP-IDF EAP client API, event-based connection/DHCP/retry and bounded failure. Validate server **CA chain AND expected identity/hostname**. Investigate whether this exact validation is supported; if not, document limitations and propose safe approach rather than weakening security silently. MikroTik cAP ax AP and FreeRADIUS at `192.168.88.116`; independent `eapol_test` EAP-TLS exchange previously succeeded. No cloud access to private infrastructure is expected.
3. Port existing Chainlink hardware control, protobuf/serial publish handling and persisted configuration incrementally; ensure stepping/sensor timing, calibration and offsets are preserved. Audit Arduino `String`, GPIO/SPI, display libraries and FFat/NVS use. Add host-side tests where feasible.
4. Provide native ESP-IDF HTTP server `POST /api/publish` with a clearly documented JSON request/response format, input validation, bounded memory use, authentication/access-control plan, and thread-safe dispatch into existing message publication flow (do not conflate with the old outbound HTTP weather example).
5. Native OTA with appropriate flash partitions, image validation, rollback, update authentication and consideration of signed firmware/secure boot supported by the real chip. Document limitations.

## Security and operational requirements
- **NEVER** commit, generate fake 'real' certs, print, expose, ask to upload, or embed actual private keys, credentials, RADIUS shared secrets, device certificates or private CA details in GitHub, logs, PRs, builds or CI artifacts.
- Real device-specific certificate, key, and CA bundle are stored **only locally** under `~/Projects/splitflap/certs/` on owner's Void Linux workstation, and are not available to the worker. Make a documented local-only provisioning process; provide placeholders/templates, `.gitignore` coverage, and avoid leaking secrets in firmware build output. Use per-device identity.
- Keep all work on `feature/esp-idf-modern`. Never push directly to `master`, never submit PR against Scott's upstream, never merge, flash, or deploy.
- Respect original license/attribution. Do not claim hardware functionality based only on a successful compilation.
- Document precise ESP-IDF/toolchain versions, build/test commands and results, migration risks, unresolved hardware information, and next-owner steps.

## Acceptance criteria for first draft PR
- Isolated ESP-IDF scaffold compiles reproducibly in documented environment (or clearly explain blocker with tested error logs).
- Existing PlatformIO build remains intact.
- Architecture/dependency audit and hardware assumptions documented.
- Hardware-specific functions and credentials are not fabricated.
- PR describes actual completed work, exact verification and concrete next milestones.

## Work handoff prompt
> Open https://github.com/pagui85/splitflap and read `docs/ESP_IDF_WORK_BRIEF.md` on branch `feature/esp-idf-modern`. Implement the **first milestone** described in the brief using native ESP-IDF/C++/FreeRTOS. Keep the legacy code buildable; create an isolated ESP-IDF scaffold, document the migration audit and verify a build. Push changes **only to** `feature/esp-idf-modern` and open a **draft PR into this fork's master**, never Scott's upstream. No real certificates/keys or secrets, no firmware flashing, no merge. Report verified results and the next steps. Continue into EAP-TLS as an isolated component only if the first milestone is solid; do not bypass RADIUS server identity validation.
