/*
 * Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
 */
#include "key_seal.h"

#include <string.h>

#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/platform_util.h"

static int derive(const uint8_t *pass, size_t pass_len, const uint8_t *salt, uint32_t iterations,
                  uint8_t kek[32]) {
    if (pass == NULL || pass_len == 0 || iterations < CF_SEAL_MIN_ITERATIONS ||
        iterations > CF_SEAL_MAX_ITERATIONS) {
        return -1;
    }
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, pass, pass_len, salt,
                                         CF_SEAL_SALT_SIZE, iterations, 32, kek) == 0
               ? 0
               : -1;
}

int cf_seal_encrypt(const uint8_t *pass, size_t pass_len, const uint8_t salt[CF_SEAL_SALT_SIZE],
                    uint32_t iterations, const uint8_t nonce[CF_SEAL_NONCE_SIZE],
                    const uint8_t *aad, size_t aad_len, const uint8_t plain[64],
                    uint8_t cipher[64], uint8_t tag[CF_SEAL_TAG_SIZE]) {
    uint8_t kek[32];
    if (derive(pass, pass_len, salt, iterations, kek) != 0) {
        mbedtls_platform_zeroize(kek, sizeof(kek));
        return -1;
    }
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, kek, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, 64, nonce, CF_SEAL_NONCE_SIZE,
                                       aad, aad_len, plain, cipher, CF_SEAL_TAG_SIZE, tag);
    }
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(kek, sizeof(kek));
    return rc == 0 ? 0 : -1;
}

int cf_seal_decrypt(const uint8_t *pass, size_t pass_len, const uint8_t salt[CF_SEAL_SALT_SIZE],
                    uint32_t iterations, const uint8_t nonce[CF_SEAL_NONCE_SIZE],
                    const uint8_t *aad, size_t aad_len, const uint8_t cipher[64],
                    const uint8_t tag[CF_SEAL_TAG_SIZE], uint8_t plain[64]) {
    uint8_t kek[32];
    memset(plain, 0, 64);
    if (derive(pass, pass_len, salt, iterations, kek) != 0) {
        mbedtls_platform_zeroize(kek, sizeof(kek));
        return -1;
    }
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, kek, 256);
    int result = -1;
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&gcm, 64, nonce, CF_SEAL_NONCE_SIZE, aad, aad_len, tag,
                                      CF_SEAL_TAG_SIZE, cipher, plain);
        result = rc == 0 ? 0 : (rc == MBEDTLS_ERR_GCM_AUTH_FAILED ? 1 : -1);
    }
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(kek, sizeof(kek));
    if (result != 0) {
        mbedtls_platform_zeroize(plain, 64);
    }
    return result;
}
