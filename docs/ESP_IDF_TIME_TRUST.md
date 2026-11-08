# S3 EAP-TLS and validity-time trust plan (milestone 2)

Status: **design investigation only**. Networking, RTC drivers and provisioning
remain inactive/unimplemented. No certificates/keys were supplied, generated,
embedded, logged or committed. The S3 does not change milestone 1's strict trust
requirements; see [EAP-TLS plan](ESP_IDF_EAP_TLS_PLAN.md) for the local-only
material contract and pinned ESP-IDF source links.

## Authentication policy

Use native EAP-TLS only on `paguiEAP`, per-device identity
`splitflap01.lab.danpapuga.com`, the local FreeIPA-issued P-256 key/cert chain,
and the intended CA trust anchor. Validate **both** that CA chain and the RADIUS
DNS identity `radius01.lab.danpapuga.com`, with validity dates and key-usage
checks enabled. Outer EAP identity, SSID, RADIUS IP and successful DHCP do not
authenticate the RADIUS certificate. No PEAP/password, public-CA-bundle,
missing-CA, hostname-bypass or disabled-time fallback is allowed.

Pinned v5.5.6 `esp_eap_client_set_domain_name` reaches
`mbedtls_ssl_set_hostname` in the Mbed TLS EAP backend; the internal TLS backend
returns unsupported. A future component must check every setter result, require
nonempty CA/client material and stop before Wi-Fi on unsupported validation.
Mbed TLS DNS matching can accept certificate wildcards/CN fallback according to
its policy, so the exact-name requirement still needs negative SAN/CN/wildcard
tests and a supported enforcement decision. Do not claim exact-SAN-only matching
from the setter's name. EAP issuer/profile restrictions alone are not a substitute
for client-side validation if exact matching is required.

Keep key/cert/CA files only on the owner's workstation under `certs/`; a later
local provisioner must verify cert/key matching without printing them, use a
dedicated versioned credential store and protect extraction/rotation/revocation.
No RADIUS shared secret belongs on the supplicant. No cloud access to FreeIPA,
the AP or RADIUS is expected or required for this milestone.

## Bootstrap problem and choices

An S3 cold boot does not establish trustworthy UTC. Persisting a last-seen time
does not measure elapsed time while power is removed. DHCP, plain SNTP, network
location and a nearby stratum-1 clock provide no cryptographic proof of freshness.
Additionally the enterprise network may not provide IP access until EAP-TLS has
already completed: obtaining time from that network first is circular.

| Approach | Trust/bootstrap advantage | Limits and proposed use |
| --- | --- | --- |
| Owner-attended secure time provisioning per cold boot | A physically controlled serial/local provisioning session can supply UTC before EAP without network access | Operator/source must be trusted; bound transfer delay, record provenance, prevent replay, account for drift, and fail closed on reboot/loss of time. Simplest initial bench policy; not an unattended solution. |
| Signed time response with pre-provisioned public verification key | A proposed nonce-bound, device-bound freshness proof need not depend on X.509 time to authenticate its signing key | Requires a separately reachable channel before EAP (e.g. local provisioner), robust randomness, timeout/delay bounds and reviewed message/key-rotation design. A stored signed timestamp alone is replayable/stale. No custom protocol implemented here. |
| NTS or another reviewed authenticated synchronization protocol | Refreshes trusted time once connectivity and initial trust exist | NTS uses TLS for key establishment and itself has a validity-time bootstrap issue. It cannot be assumed to solve pre-EAP reachability or cold boot. ESP-IDF SNTP API is not evidence of an NTS client. Investigate an actual supported implementation separately. |
| Battery-backed RTC, securely commissioned | Maintains UTC across MCU power loss without pre-EAP IP access | RTC is not a cryptographic trust anchor: time can be changed over I2C/physical access, battery loss and drift must be detected. Use only with a defined physical-threat model, authenticated initial set, validity flags and bounded holdover. Optional future Buddy V2 provision. |
| Plain SNTP, compile date, saved NVS timestamp | Useful diagnostics/lower-bound hints | **Insufficient for trusted boot time**; never authorize EAP by silently trusting these or disabling date validation. |

The NTS analysis follows [RFC 8915](https://datatracker.ietf.org/doc/html/rfc8915),
especially its TLS key establishment and section 8.5 bootstrap discussion. This
project does **not** adopt certificate-date exceptions discussed by that RFC.
The proposed initial policy is owner-attended trusted UTC after every cold boot;
unattended EAP remains blocked until a reviewed retained-time/bootstrap design is
implemented. No extra RTC needs to be purchased for this milestone.

## Optional future RTC comparison

| Candidate | Relevant capability / validity signal | Design issues before any integration |
| --- | --- | --- |
| DS3231 | Battery switchover, integrated temperature-compensated oscillator; oscillator-stop flag OSF indicates suspect time | Reject OSF=1 and uncommissioned/out-of-range time. Review board size, bus/pull-up voltage, backup-cell circuit, drift bounds and battery maintenance. |
| RV-3028-C7 | Low-power I2C RTC with backup supply, calendar/UNIX-time registers; PORF marks power-reset/time corruption | Reject PORF=1 or uncommissioned time. Review backup-switchover configuration, battery circuit/trickle-charge settings, offset/drift and initialization sequencing. |

Primary references: [DS3231 datasheet, status register](https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf),
[RV-3028-C7 application manual, POR/backup sections](https://www.microcrystal.com/fileadmin/Media/Products/RTC/App.Manual/RV-3028-C7_App-Manual.pdf).
Neither cleared status flag proves authentic UTC. A future 3.3 V I2C design must
avoid pulling SDA/SCL to 5 V and must review non-rechargeable-cell charging hazards.
No RTC pin assignments, schematic, PCB or driver are introduced here.

## Proposed fail-closed state and verification gates

At boot start `TIME_UNTRUSTED`; do not start EAP. Accept time only from an approved
commissioning/retention path, then maintain a UTC estimate, monotonic elapsed
clock, provenance and a conservative uncertainty/holdover bound. Reject rollback,
invalid RTC flags, implausible jumps, uncertainty expiry and replay. Ordinary
TLS point-time validation alone does not enforce a custom uncertainty interval;
that policy needs a supported implementation and boundary tests before shipping.
Never substitute last-seen NVS time or unauthenticated SNTP on failure.

Only after time and certificate/identity policy are ready should a dedicated
network owner start EAP-TLS with bounded connection/DHCP deadlines, retry budget
and backoff. Mark ready only after verified EAP and DHCP. If trust expires, stop
authenticated service use and require recovery under the reviewed policy; do not
clear verification errors to reconnect. Keep network callbacks out of motor state.

Before enabling: prove success with the real local P-256 chain and prove rejection
of same-CA wrong DNS SAN, absent/wrong CA, expired/not-yet-valid chain, CN-only and
wildcard policy violations, wrong client key, unsupported backend, boot without
trusted time, stale/rollback RTC, authenticated-time replay/delay, AP loss and DHCP
timeout. Verify logs contain no credential bytes. These tests are outstanding,
not simulated by compilation or the existing desktop `eapol_test` success.
