#ifndef CF_BACKUP_CODEC_H
#define CF_BACKUP_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CF_BACKUP_OK = 0,
    CF_BACKUP_INVALID = -1,
    CF_BACKUP_IO = -2,
    CF_BACKUP_AUTH = -3,
    CF_BACKUP_MEMORY = -4,
    CF_BACKUP_CRYPTO = -5
};
#define CF_BACKUP_PASSWORD_MIN 12u
#define CF_BACKUP_PASSWORD_MAX 64u
#define CF_BACKUP_ITERATIONS 600000u
#define CF_BACKUP_CHUNK 4096u
#define CF_BACKUP_HEADER 80u

typedef int (*cf_backup_read_fn)(void *, uint32_t, uint8_t *, size_t);
typedef int (*cf_backup_write_fn)(void *, uint32_t, const uint8_t *, size_t);
typedef int (*cf_backup_random_fn)(void *, uint8_t *, size_t);

/* Files are caller-owned and must be opened in binary mode. No plaintext is
 * written to the file. The device binding is public, not an encryption key.
 * Export requires 12..64 printable ASCII bytes; secrets are never logged. */
/* Optional progress observer, called from the caller's task. Stage KDF reports the
 * milliseconds spent deriving the key so far (total 0: the caller estimates); stage DATA
 * reports bytes processed out of the store size. Pass NULL to disable. */
#define CF_BACKUP_STAGE_KDF 1
#define CF_BACKUP_STAGE_DATA 2
typedef void (*cf_backup_progress_fn)(void *ctx, int stage, uint32_t done, uint32_t total);
void cf_backup_set_progress(cf_backup_progress_fn fn, void *ctx);

/* While enabled, a key derived for a file is remembered (with its salt and a hash of the
 * password) so saving and verifying the same backup derives it once. Disabling wipes it.
 * Off by default. cf_backup_kdf_runs() counts real derivations (for tests). */
void cf_backup_key_cache(int enable);
uint32_t cf_backup_kdf_runs(void);

int cf_backup_export(FILE *file, const uint8_t *password, size_t password_len,
                     const uint8_t device[32], cf_backup_read_fn read,
                     void *ctx, cf_backup_random_fn random, void *random_ctx);
int cf_backup_verify(FILE *file, const uint8_t *password, size_t password_len,
                     const uint8_t device[32]);
/* Authenticates the entire file BEFORE the first write. Each chunk is checked
 * again while applying. Caller must persist a recovery guard before calling:
 * media/I/O/power failure after writes start requires retrying restoration. */
int cf_backup_restore(FILE *file, const uint8_t *password, size_t password_len,
                      const uint8_t device[32], cf_backup_write_fn write,
                      void *ctx);

#ifdef __cplusplus
}
#endif
#endif
