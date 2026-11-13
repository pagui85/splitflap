// SPDX-License-Identifier: Apache-2.0
#include "credentials.hpp"
#include "persistent.hpp"

#include <cstdio>
#include <cstring>
#include "esp_random.h"
extern "C" {
#include "mbedtls/constant_time.h"
}
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecp.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace bringup::ota {
namespace {
constexpr char kNamespace[] = "ota_auth";
constexpr char kCredentialKey[] = "identity";
struct Credentials {
    uint32_t version = 1;
    uint8_t token_digest[kDigestSize]{};
    char certificate[1024]{};
    char key[384]{};
};
Credentials credentials;
nvs_handle_t storage = 0;
bool loaded = false;

int entropy(void *, unsigned char *output, size_t size)
{
    esp_fill_random(output, size);
    return 0;
}

int create_identity(Credentials &next, char *pin, char *token)
{
    mbedtls_ctr_drbg_context drbg;
    mbedtls_pk_context key;
    mbedtls_x509write_cert cert;
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&cert);
    uint8_t random[kDigestSize]{};
    const auto result = [&]() {
        constexpr unsigned char label[] = "splitflap-u213-ota";
        int rc = mbedtls_ctr_drbg_seed(&drbg, entropy, nullptr, label, sizeof(label) - 1);
        if (rc != 0) return rc;
        rc = mbedtls_ctr_drbg_random(&drbg, random, sizeof(random));
        if (rc != 0) return rc;
        hex_encode(random, sizeof(random), token);
        rc = mbedtls_sha256(reinterpret_cast<const uint8_t *>(token), 64, next.token_digest, 0);
        if (rc != 0) return rc;

        rc = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
        if (rc == 0) rc = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                                              mbedtls_ctr_drbg_random, &drbg);
        if (rc != 0) return rc;
        mbedtls_x509write_crt_set_version(&cert, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_md_alg(&cert, MBEDTLS_MD_SHA256);
        mbedtls_x509write_crt_set_subject_key(&cert, &key);
        mbedtls_x509write_crt_set_issuer_key(&cert, &key);
        constexpr char name[] = "CN=Splitflap U213 OTA";
        rc = mbedtls_x509write_crt_set_subject_name(&cert, name);
        if (rc == 0) rc = mbedtls_x509write_crt_set_issuer_name(&cert, name);
        // The client verifies the complete DER fingerprint out of band.
        if (rc == 0) rc = mbedtls_x509write_crt_set_validity(&cert, "20240101000000", "20991231235959");
        if (rc == 0) rc = mbedtls_ctr_drbg_random(&drbg, random, 16);
        random[0] = (random[0] & 0x7f) | 1;
        if (rc == 0) rc = mbedtls_x509write_crt_set_serial_raw(&cert, random, 16);
        if (rc == 0) rc = mbedtls_x509write_crt_pem(&cert, reinterpret_cast<uint8_t *>(next.certificate),
                                                   sizeof(next.certificate), mbedtls_ctr_drbg_random, &drbg);
        if (rc == 0) rc = mbedtls_pk_write_key_pem(&key, reinterpret_cast<uint8_t *>(next.key), sizeof(next.key));
        if (rc != 0) return rc;
        mbedtls_x509_crt parsed;
        mbedtls_x509_crt_init(&parsed);
        rc = mbedtls_x509_crt_parse(&parsed, reinterpret_cast<const uint8_t *>(next.certificate),
                                    std::strlen(next.certificate) + 1);
        if (rc == 0) rc = mbedtls_sha256(parsed.raw.p, parsed.raw.len, random, 0);
        if (rc == 0) hex_encode(random, sizeof(random), pin);
        mbedtls_x509_crt_free(&parsed);
        return rc;
    }();
    mbedtls_platform_zeroize(random, sizeof(random));
    mbedtls_x509write_crt_free(&cert);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&drbg);
    return result;
}
}

void hex_encode(const uint8_t *bytes, std::size_t size, char *output)
{
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < size; ++i) {
        output[2 * i] = digits[bytes[i] >> 4];
        output[2 * i + 1] = digits[bytes[i] & 15];
    }
    output[2 * size] = '\0';
}

bool hex_decode(const char *text, std::size_t size, uint8_t *output)
{
    if (std::strlen(text) != 2 * size) return false;
    for (std::size_t i = 0; i < size; ++i) {
        unsigned byte = 0;
        for (unsigned j = 0; j < 2; ++j) {
            const char ch = text[2 * i + j];
            if (ch >= '0' && ch <= '9') byte = 16 * byte + ch - '0';
            else if (ch >= 'a' && ch <= 'f') byte = 16 * byte + ch - 'a' + 10;
            else return false;
        }
        output[i] = static_cast<uint8_t>(byte);
    }
    return true;
}

esp_err_t credentials_open()
{
    auto result = nvs_flash_init_partition(kKeysPartition);
    if (result != ESP_OK) return result;
    result = nvs_open_from_partition(kKeysPartition, kNamespace, NVS_READWRITE, &storage);
    if (result != ESP_OK) return result;
    size_t size = sizeof(credentials);
    result = nvs_get_blob(storage, kCredentialKey, &credentials, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (result != ESP_OK) return result;
    if (size != sizeof(credentials) || credentials.version != 1 ||
        !credentials.certificate[0] || !credentials.key[0] ||
        credentials.certificate[sizeof(credentials.certificate) - 1] || credentials.key[sizeof(credentials.key) - 1])
        return ESP_ERR_INVALID_VERSION;
    loaded = true;
    return ESP_OK;
}

bool provisioned() { return loaded; }
const char *certificate() { return credentials.certificate; }
const char *private_key() { return credentials.key; }

esp_err_t provision()
{
    if (!storage || loaded) return ESP_ERR_INVALID_STATE;
    Credentials next{};
    char token[65]{};
    char pin[65]{};
    const int identity_result = create_identity(next, pin, token);
    auto result = identity_result == 0 ? ESP_OK : ESP_FAIL;
    if (result == ESP_OK) result = nvs_set_blob(storage, kCredentialKey, &next, sizeof(next));
    if (result == ESP_OK) result = nvs_commit(storage);
    if (result == ESP_OK) {
        // Network handlers use only the credentials loaded at boot.
        std::printf("ota_certificate_sha256=%s\nota_token=%s\n"
                    "Save these locally with owner-only access. Reboot to enable HTTPS OTA.\n", pin, token);
    } else if (identity_result != 0) {
        std::printf("identity_generation_error=%d\n", identity_result);
    }
    mbedtls_platform_zeroize(token, sizeof(token));
    mbedtls_platform_zeroize(&next, sizeof(next));
    return result;
}

bool authorized(const char *token)
{
    if (!loaded || std::strlen(token) != 64) return false;
    uint8_t digest[kDigestSize];
    if (mbedtls_sha256(reinterpret_cast<const uint8_t *>(token), 64, digest, 0) != 0) return false;
    return mbedtls_ct_memcmp(digest, credentials.token_digest, sizeof(digest)) == 0;
}
}
