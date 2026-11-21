/*
 * Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
 *
 * Read-only view of the OATH application for the on-device "Codes" screen. Secrets never
 * leave oath.c: the screen gets names, display properties and computed codes only.
 * Callers must not run concurrently with an APDU (the UI calls these only when no USB
 * command is in flight).
 */
#ifndef STK_OATH_SCREEN_H
#define STK_OATH_SCREEN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STK_OATH_NAME_MAX 64
#define STK_OATH_SCREEN_MAX 16

typedef struct {
    char name[STK_OATH_NAME_MAX + 1];   /* display name: "issuer:account", period prefix removed */
    uint8_t name_raw_len;               /* stored name length, period prefix included */
    char name_raw[STK_OATH_NAME_MAX + 1];
    uint8_t totp;                       /* 1 TOTP, 0 HOTP (HOTP is listed but never computed) */
    uint8_t digits;
    uint32_t period;                    /* seconds, TOTP only */
    uint8_t touch;                      /* the owner asked for a touch before each code */
} stk_oath_entry_t;

typedef enum {
    STK_OATH_SCREEN_OK = 0,
    STK_OATH_SCREEN_PROTECTED = 1,      /* an access code is set: codes are not shown on screen */
    STK_OATH_SCREEN_ERROR = -1
} stk_oath_screen_status;

/* Fills up to `cap` entries, sorted by name; *count is the number filled. */
stk_oath_screen_status stk_oath_screen_list(stk_oath_entry_t *out, size_t cap, size_t *count);

/* Computes the TOTP code of the credential whose stored name is `name_raw` at `unix_s`.
 * Refuses HOTP (showing it would advance the counter) and protected applications. */
stk_oath_screen_status stk_oath_screen_code(const char *name_raw, size_t name_raw_len,
                                            uint64_t unix_s, uint32_t *code);

#ifdef __cplusplus
}
#endif

#endif
