/*
 * Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
 *
 * Passphrase sealing of the device master keys: PBKDF2-HMAC-SHA256 derives a key-encryption
 * key, AES-256-GCM encrypts the 64-byte key pair, and the record header is authenticated as
 * additional data so its KDF parameters cannot be altered without detection.
 */
#ifndef CF_KEY_SEAL_H
#define CF_KEY_SEAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CF_SEAL_SALT_SIZE 16u
#define CF_SEAL_NONCE_SIZE 12u
#define CF_SEAL_TAG_SIZE 16u
#define CF_SEAL_MIN_ITERATIONS 1u          /* host tests use few; firmware calibrates */
#define CF_SEAL_MAX_ITERATIONS 5000000u    /* refuse records that would stall unlock */

/* 0 on success. `plain` is 64 bytes; `aad` covers the record header. */
int cf_seal_encrypt(const uint8_t *pass, size_t pass_len, const uint8_t salt[CF_SEAL_SALT_SIZE],
                    uint32_t iterations, const uint8_t nonce[CF_SEAL_NONCE_SIZE],
                    const uint8_t *aad, size_t aad_len, const uint8_t plain[64],
                    uint8_t cipher[64], uint8_t tag[CF_SEAL_TAG_SIZE]);

/* 0 on success, 1 when the passphrase is wrong or the record was altered, <0 on error.
 * `plain` is zeroized unless the result is 0. */
int cf_seal_decrypt(const uint8_t *pass, size_t pass_len, const uint8_t salt[CF_SEAL_SALT_SIZE],
                    uint32_t iterations, const uint8_t nonce[CF_SEAL_NONCE_SIZE],
                    const uint8_t *aad, size_t aad_len, const uint8_t cipher[64],
                    const uint8_t tag[CF_SEAL_TAG_SIZE], uint8_t plain[64]);

#ifdef __cplusplus
}
#endif

#endif
