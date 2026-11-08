# Native ESP-IDF scaffold (milestone 1)

This is an isolated, diagnostic-only C++ application. It initializes NVS without
an erase fallback, creates the default ESP event loop and starts a FreeRTOS health
task. It does **not** control motors, read sensors, load offsets, speak the serial
protocol, drive the LCD, start Wi-Fi, serve HTTP, or update firmware. Do not use
this image on the installed display. A successful build is not a hardware test.

The existing PlatformIO application, source filters, generated protobufs and
dependencies remain in their original locations. From the repository root its
build command remains `pio run -e chainlink`.

## Pinned toolchain and build

Use Espressif **ESP-IDF v5.5.6**, commit
`880a203ae603d08a94d06aa01e0d6761958e5e9e`, target `esp32`.
This recent maintenance release of the supported 5.5 series keeps the first port
on the established 5.x APIs instead of combining it with the 6.x migration.
It is not the newest major release. Re-evaluate supported releases and security
updates at each migration milestone; the CMake version check makes upgrades
explicit. See the [release](https://github.com/espressif/esp-idf/releases/tag/v5.5.6)
and [support policy](https://github.com/espressif/esp-idf/blob/master/SUPPORT_POLICY.md).

On a Linux host with Git, Python 3, a C/C++ host compiler and Espressif's documented
[prerequisites](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32/get-started/linux-macos-setup.html):

```sh
# SDK installation outside this repository (once)
git clone --branch v5.5.6 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v5.5.6"
cd "$HOME/esp/esp-idf-v5.5.6"
./install.sh esp32
. ./export.sh

# From your checkout, with no device connected
cd "$HOME/Projects/splitflap/firmware/esp-idf"
idf.py set-target esp32
idf.py build
idf.py size
```

`set-target` regenerates this project's ignored `sdkconfig` and build directory;
it does not change `platformio.ini`. To repeat a build from clean configuration,
use a fresh checkout or remove only this project's generated `build/` and
`sdkconfig` first. No `secrets.h`, certificate, submodule from the splitflap repo,
Arduino core or managed component is required for this application.

The native CI workflow uses `espressif/idf:v5.5.6`, checks the SDK commit, and
builds only; it has no flashing, upload, private provisioning or artifact step.
See [verification](../../docs/ESP_IDF_VERIFICATION.md) for actual results and any
environment limitations, rather than treating the presence of CI as a pass.

## Layout and startup contract

| Location | Responsibility |
| --- | --- |
| `main/app_main.cpp` | Ordered startup, ESP logging, error propagation |
| `components/scaffold_platform` | Non-destructive NVS initialization only |
| `components/scaffold_runtime` | One C++ task, private ESP event base, fixed-size health events |
| `sdkconfig.defaults` | Provisional image defaults, independent of legacy flags |

`app_main` initializes storage before starting runtime services. Any storage
error stops application startup and logs its error name; it never calls
`nvs_flash_erase` or formats FFat. The task uses an IDF stack size in **bytes**,
blocks between five-second health reports, and posts copied event payloads
without blocking. Task creation and handler registration failures propagate to
startup. `runtime::start()` is a single-caller boot API; a second successful
start is rejected. Diagnostic events may be dropped if the event loop is full.
This task is not a timing template for the future motor loop.

## Provisional hardware/image boundary

`esp32` is based on the legacy `esp32dev` environment, original dual-core
T-Display documentation and Buddy schematic. The owner's exact module, flash
size, silicon revision, wiring and boot state have **not** been measured.
Defaults use a conservative **2 MiB image header**, 115200-baud diagnostic
console, and ESP-IDF's factory-only single-app partition table (1 MiB app).
These are scaffold build settings, not claims about installed flash capacity.
The legacy serial protocol uses 230400 baud and a different logging model.

The factory-only layout has **no FFat partition and no OTA slots**. It is not a
compatible replacement for legacy `default_ffat.csv`. Before any later hardware
trial, inventory the real chip and flash, back up calibration and the actual
partition table, and design/test an explicit storage migration and recovery
path. NVS initialization without erasing does not make flashing a changed
partition layout safe. No flashing is part of this milestone.

For migration findings, hardware questions and the next acceptance gates see
[the audit](../../docs/ESP_IDF_MIGRATION_AUDIT.md). EAP-TLS is deliberately not
enabled; its security/provisioning plan is [documented separately](../../docs/ESP_IDF_EAP_TLS_PLAN.md).
