#ifndef CF_KEY_STORE_H
#define CF_KEY_STORE_H

#include <stddef.h>
#include <stdint.h>

#define CF_KEY_PAGE_SIZE 4096u
#define CF_KEY_PAGE_COUNT 2u
#define CF_KEY_RECORD_SIZE 88u
#define CF_KEY_PAYLOAD_SIZE 64u
#define CF_KEY_VERSION 1u
#define CF_KEY_COMMIT_MARKER 0x43464B31u

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CF_KEY_EMPTY = 0,
    CF_KEY_READY = 1,
    CF_KEY_CORRUPT = 2,
    CF_KEY_IO_ERROR = 3,
    CF_KEY_INVALID_ARGUMENT = 4,
    CF_KEY_LOCKED = 5,              /* keys are sealed: cf_key_store_unlock() with the passphrase */
    CF_KEY_WRONG_PASSPHRASE = 6
} cf_key_status;

/* Sealed record (version 2): keys encrypted under a passphrase, see key_seal.h. */
#define CF_KEY_SEALED_VERSION 2u
#define CF_KEY_SEALED_SIZE 136u

/*
 * The flash adapter owns the two key pages. Offsets are relative to the first
 * key page. Erase and program must behave like NOR flash: erase sets bytes to
 * 0xFF and programming can only clear bits.
 */
typedef struct {
    void *ctx;
    int (*read)(void *ctx, uint32_t offset, uint8_t *out, size_t len);
    int (*program)(void *ctx, uint32_t offset, const uint8_t *data, size_t len);
    int (*erase)(void *ctx, uint32_t offset, size_t len);
    /* Returns 1 only when the entire credential filesystem is erased. */
    int (*database_erased)(void *ctx);
    /* Returns 0 and fills len bytes on success. */
    int (*random)(void *ctx, uint8_t *out, size_t len);
} cf_key_flash;

typedef struct {
    uint8_t key1[32];
    uint8_t key2[32];
} cf_device_keys;

uint32_t cf_key_crc32(const uint8_t *data, size_t len);

/* Serializes the fixed record and fills the unused tail with 0xFF. */
void cf_key_record_encode(const cf_device_keys *keys,
                          uint8_t record[CF_KEY_PAGE_SIZE]);

/*
 * Decodes one record. The commit marker, CRC, version, payload length and the
 * secp256k1 scalar range are all validated. Output is zeroized on failure.
 */
cf_key_status cf_key_record_decode(const uint8_t record[CF_KEY_PAGE_SIZE],
                                   cf_device_keys *out);

/*
 * Reads both mirrored pages and applies the recovery policy. This function
 * never writes to flash.
 */
cf_key_status cf_key_store_load(const cf_key_flash *flash, cf_device_keys *out);

/*
 * Provisions a new identity only when both key pages and the credential
 * filesystem are erased. A committed copy is verified before the output is
 * published. Existing or corrupt data is never overwritten.
 */
cf_key_status cf_key_store_provision(const cf_key_flash *flash,
                                     cf_device_keys *out);

/*
 * Version 2: like cf_key_store_load(), but returns CF_KEY_LOCKED (with `out` zeroed) when
 * the winning record is sealed. Records carry a generation; the highest valid one wins, so
 * an update interrupted by power loss resolves to either the old or the new state.
 */

/*
 * Applies the load policy to two key pages held in memory (for example inside a backup):
 * READY when the winner is plain, LOCKED when it is sealed, CORRUPT otherwise. Never
 * decrypts anything.
 */
cf_key_status cf_key_pages_status(const uint8_t page_a[CF_KEY_PAGE_SIZE],
                                  const uint8_t page_b[CF_KEY_PAGE_SIZE]);

/*
 * Opens the store. Plain records ignore the passphrase. On success with a sealed record,
 * any page that is not an exact copy of the winner (for example a plaintext page left by
 * an interrupted sealing) is rewritten from it.
 */
cf_key_status cf_key_store_unlock(const cf_key_flash *flash, const uint8_t *pass,
                                  size_t pass_len, cf_device_keys *out);

/*
 * Sets, changes or removes the passphrase without changing the device identity.
 * `old_pass` must open the current record (ignored when it is plain). A new_len of 0
 * writes a plain record. `iterations` is the PBKDF2 cost for a sealed record.
 * The page that is not the current winner is written and verified first, then the other.
 */
cf_key_status cf_key_store_change_passphrase(const cf_key_flash *flash,
                                             const uint8_t *old_pass, size_t old_len,
                                             const uint8_t *new_pass, size_t new_len,
                                             uint32_t iterations);

#ifdef __cplusplus
}
#endif

#endif
