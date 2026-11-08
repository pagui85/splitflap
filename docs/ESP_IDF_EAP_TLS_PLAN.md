# EAP-TLS investigation and local-only provisioning plan

Milestone 2 adds [S3 time/RTC trust analysis](ESP_IDF_TIME_TRUST.md). The
authentication component remains unimplemented and inactive on both targets.

Status: **investigated, not implemented or enabled in this milestone**. No
credentials were available or requested. No Wi-Fi/authentication result is
claimed from a scaffold build. The prior `eapol_test` result in the brief is
owner-provided context, not a test performed here.

## Verified upstream API boundary

Inspected ESP-IDF v5.5.6 (`880a203ae603d08a94d06aa01e0d6761958e5e9e`):

- [`esp_eap_client.h`](https://github.com/espressif/esp-idf/blob/v5.5.6/components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h)
  provides CA/client certificate/identity APIs, EAP method selection,
  `esp_eap_client_set_disable_time_check` and
  `esp_eap_client_set_domain_name`.
- [`esp_eap_client.c`](https://github.com/espressif/esp-idf/blob/v5.5.6/components/wpa_supplicant/esp_supplicant/src/esp_eap_client.c)
  returns `ESP_ERR_NOT_SUPPORTED` for the domain setter with
  `CONFIG_TLS_INTERNAL_CLIENT`; a future component must fail closed on this
  result, never continue without hostname validation.
- [`tls_mbedtls.c`](https://github.com/espressif/esp-idf/blob/v5.5.6/components/wpa_supplicant/esp_supplicant/src/crypto/tls_mbedtls.c)
  passes `cfg->domain_match` to `mbedtls_ssl_set_hostname`. CA provisioning is
  mandatory: that implementation chooses `MBEDTLS_SSL_VERIFY_NONE` when no CA
  blob is supplied. A caller must reject missing/empty CA before connecting.
  Use the Mbed TLS backend with certificate and key-usage verification enabled.
- This is TLS DNS-name verification, not an IP-address check or EAP outer
  identity check. Review SAN/CN and wildcard behavior in the pinned Mbed TLS
  implementation against the exact-identity requirement. A name setter returning
  success alone does not prove the peer identity was authenticated.

Expected RADIUS DNS identity is `radius01.lab.danpapuga.com`; EAP client identity
is `splitflap01.lab.danpapuga.com` on SSID `paguiEAP` (public task parameters,
not credentials). The AP and RADIUS IP do not replace certificate identity
verification. Trust only the intended owner-supplied CA bundle, not a general
public CA fallback. Do not disable validity dates to work around boot-time clock
problems; design a trusted clock bootstrap/reboot policy first. Unauthenticated
SNTP alone must not be treated as proof of certificate validity time.

## Local-only material and eventual provisioner contract

The owner already keeps material at `~/Projects/splitflap/certs/` on Void Linux.
That directory remains local and ignored. Do not copy any real CA, client
certificate, key, password or RADIUS secret into GitHub, this worker, CI or a PR.
RADIUS shared secrets are not needed by the ESP32 supplicant.

The future local provisioner should accept **paths**, not PEM bodies on command
lines. Its non-secret configuration template can contain:

```json
{
  "ssid": "paguiEAP",
  "identity": "splitflap01.lab.danpapuga.com",
  "expected_radius_dns_name": "radius01.lab.danpapuga.com",
  "ca_file": "<local CA bundle path>",
  "client_cert_file": "<local per-device certificate path>",
  "client_key_file": "<local EC P-256 key path>"
}
```

This is a proposed input contract, **not an implemented provisioning command**.
Until that component exists, leave the files where they are and do not embed
them with CMake `EMBED_TXTFILES`. Use local directory permissions 0700 and
private-key permissions 0600. The provisioner must validate certificate/key
matching and supported P-256 material without printing it, use a dedicated
versioned storage namespace/partition, and provide rotation/revocation/recovery
without erasing calibration. Review NVS/flash encryption and physical extraction
risks on the actual chip before storing keys. Build/ELF/bin/flash dumps become
sensitive if credentials are embedded later and must never become CI artifacts.

Ignore rules cover `/certs/`, `/provisioning/`, native equivalents, PEM/key/P12/PFX
files, local sdkconfig variants and real `secrets.h`; templates remain tracked.
Ignore rules are a guard, not a replacement for reviewing every staged diff.

## Next component acceptance gates

1. Validate required material and expected identity before starting Wi-Fi; allow
   EAP-TLS only, no PEAP/password fallback, no insecure trust fallback. Check every
   API result and stop on unsupported hostname verification.
2. Register Wi-Fi/IP events with a dedicated owner task, bounded retry/backoff,
   total connection and DHCP deadlines, clean cancellation, and no repeated
   reconnect loop after exhaustion. Do not mark ready until DHCP succeeds.
3. Prove rejection of absent/wrong CA, wrong server DNS SAN, expired/not-yet-valid
   server certificate, untrusted intermediate, wrong client key/cert and unsupported
   backend. Verify hostname mismatch with the **same valid CA**. Include CN-only
   and wildcard cases to resolve exact-identity policy. Never weaken policy to
   make the test pass.
4. Locally test the real FreeIPA-issued P-256 client chain against FreeRADIUS.
   Verify reconnect, DHCP timeout, AP loss, revocation/rotation policy and that
   errors/logs never expose credential data. Only then connect the native command
   path to motors in a later milestone.

This investigation identifies a supported hostname-validation path, but trusted
time bootstrapping, exact-name policy and negative handshake tests remain open.
Networking is therefore deferred rather than shipping a partially verified
authentication component.
