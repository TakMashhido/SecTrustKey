/*
 * Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
 *
 * RFC 4226 / RFC 6238 one-time codes, used to show TOTP codes on the device screen.
 * Pure function over a secret already decrypted by the OATH application.
 */
#ifndef STK_TOTP_H
#define STK_TOTP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hash selectors, identical to the OATH application's algorithm byte (low nibble). */
#define STK_TOTP_SHA1   0x01
#define STK_TOTP_SHA256 0x02
#define STK_TOTP_SHA512 0x03

/* Computes the dynamic-truncation code for `counter` (the TOTP time step, or the HOTP
 * counter) with `digits` decimal digits (6..10). Returns 0 and writes *code on success,
 * negative on bad arguments or a hash failure. */
int stk_otp_code(uint8_t alg, const uint8_t *secret, size_t secret_len, uint64_t counter,
                 uint8_t digits, uint32_t *code);

/* The TOTP time step for Unix time `unix_s` and `period` seconds (0 means 30). */
uint64_t stk_totp_step(uint64_t unix_s, uint32_t period);

/* Seconds left in the current step, 1..period. */
uint32_t stk_totp_seconds_left(uint64_t unix_s, uint32_t period);

#ifdef __cplusplus
}
#endif

#endif
