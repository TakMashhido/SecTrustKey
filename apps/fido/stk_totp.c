/*
 * Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
 */
#include "stk_totp.h"

#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"

static const mbedtls_md_info_t *otp_md(uint8_t alg) {
    switch (alg) {
        case STK_TOTP_SHA1:   return mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
        case STK_TOTP_SHA256: return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        case STK_TOTP_SHA512: return mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
        default:              return NULL;
    }
}

int stk_otp_code(uint8_t alg, const uint8_t *secret, size_t secret_len, uint64_t counter,
                 uint8_t digits, uint32_t *code) {
    if (secret == NULL || code == NULL || digits < 6 || digits > 10) {
        return -1;
    }
    const mbedtls_md_info_t *md = otp_md(alg);
    if (md == NULL) {
        return -1;
    }
    uint8_t msg[8];
    for (int i = 7; i >= 0; --i) {
        msg[i] = (uint8_t)(counter & 0xff);
        counter >>= 8;
    }
    uint8_t mac[64];
    if (mbedtls_md_hmac(md, secret, secret_len, msg, sizeof(msg), mac) != 0) {
        return -1;
    }
    const size_t mac_len = mbedtls_md_get_size(md);
    const uint8_t offset = mac[mac_len - 1] & 0x0f;
    const uint32_t binary = ((uint32_t)(mac[offset] & 0x7f) << 24) |
                            ((uint32_t)mac[offset + 1] << 16) |
                            ((uint32_t)mac[offset + 2] << 8) | (uint32_t)mac[offset + 3];
    mbedtls_platform_zeroize(mac, sizeof(mac));
    uint64_t modulus = 1;
    for (uint8_t i = 0; i < digits; ++i) {
        modulus *= 10;
    }
    *code = (uint32_t)(binary % modulus);
    return 0;
}

uint64_t stk_totp_step(uint64_t unix_s, uint32_t period) {
    return unix_s / (period ? period : 30u);
}

uint32_t stk_totp_seconds_left(uint64_t unix_s, uint32_t period) {
    const uint32_t p = period ? period : 30u;
    return p - (uint32_t)(unix_s % p);
}
