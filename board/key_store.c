#include "key_store.h"

#include <string.h>

#include "key_seal.h"

static const uint8_t kMagic[8] = {'C', 'F', 'K', 'E', 'Y', '0', '0', '1'};
static const uint8_t kSealedMagic[8] = {'C', 'F', 'K', 'E', 'Y', '0', '0', '2'};
static const uint8_t kSecp256k1Order[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
    0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
};

static void put_u32_le(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xFF);
    out[1] = (uint8_t)((value >> 8) & 0xFF);
    out[2] = (uint8_t)((value >> 16) & 0xFF);
    out[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t get_u32_le(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

static int all_bytes(const uint8_t *data, size_t len, uint8_t value) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= (uint8_t)(data[i] ^ value);
    }
    return diff == 0;
}

static int scalar_valid(const uint8_t scalar[32]) {
    int nonzero = 0;
    for (size_t i = 0; i < 32; ++i) {
        nonzero |= scalar[i];
    }
    if (nonzero == 0) {
        return 0;
    }
    for (size_t i = 0; i < 32; ++i) {
        if (scalar[i] < kSecp256k1Order[i]) {
            return 1;
        }
        if (scalar[i] > kSecp256k1Order[i]) {
            return 0;
        }
    }
    return 0; /* exactly the group order is not a private scalar */
}

uint32_t cf_key_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void cf_key_record_encode(const cf_device_keys *keys,
                          uint8_t record[CF_KEY_PAGE_SIZE]) {
    memset(record, 0xFF, CF_KEY_PAGE_SIZE);
    if (keys == NULL) {
        return;
    }
    memcpy(record, kMagic, sizeof(kMagic));
    put_u32_le(record + 8, CF_KEY_VERSION);
    put_u32_le(record + 12, CF_KEY_PAYLOAD_SIZE);
    memcpy(record + 16, keys->key1, 32);
    memcpy(record + 48, keys->key2, 32);
    const uint32_t crc = cf_key_crc32(record, 80);
    put_u32_le(record + 80, crc);
    put_u32_le(record + 84, CF_KEY_COMMIT_MARKER);
}

cf_key_status cf_key_record_decode(const uint8_t record[CF_KEY_PAGE_SIZE],
                                   cf_device_keys *out) {
    cf_device_keys keys;
    memset(&keys, 0, sizeof(keys));

    if (record == NULL || out == NULL) {
        return CF_KEY_INVALID_ARGUMENT;
    }
    if (memcmp(record, kMagic, sizeof(kMagic)) != 0 ||
        get_u32_le(record + 8) != CF_KEY_VERSION ||
        get_u32_le(record + 12) != CF_KEY_PAYLOAD_SIZE ||
        get_u32_le(record + 84) != CF_KEY_COMMIT_MARKER) {
        return CF_KEY_CORRUPT;
    }
    if (get_u32_le(record + 80) != cf_key_crc32(record, 80)) {
        return CF_KEY_CORRUPT;
    }
    if (all_bytes(record + 16, 32, 0x00) || all_bytes(record + 16, 32, 0xFF)) {
        return CF_KEY_CORRUPT;
    }
    if (!scalar_valid(record + 48)) {
        return CF_KEY_CORRUPT;
    }
    memcpy(keys.key1, record + 16, 32);
    memcpy(keys.key2, record + 48, 32);
    *out = keys;
    return CF_KEY_READY;
}

static cf_key_status read_page(const cf_key_flash *flash, uint32_t offset,
                               uint8_t out[CF_KEY_PAGE_SIZE]) {
    if (flash->read(flash->ctx, offset, out, CF_KEY_PAGE_SIZE) != 0) {
        return CF_KEY_IO_ERROR;
    }
    return CF_KEY_READY;
}

/* What one page holds. A plain (version 1) record counts as generation 0. */
typedef struct {
    int valid;
    int sealed;
    uint32_t generation;
    cf_device_keys keys;     /* plain records only */
} page_slot;

static int keys_valid(const cf_device_keys *keys) {
    return !all_bytes(keys->key1, 32, 0x00) && !all_bytes(keys->key1, 32, 0xFF) &&
           scalar_valid(keys->key2);
}

static void decode_slot(const uint8_t page[CF_KEY_PAGE_SIZE], page_slot *slot) {
    memset(slot, 0, sizeof(*slot));
    if (cf_key_record_decode(page, &slot->keys) == CF_KEY_READY) {
        slot->valid = 1;
        return;
    }
    memset(&slot->keys, 0, sizeof(slot->keys));
    if (memcmp(page, kSealedMagic, sizeof(kSealedMagic)) != 0 ||
        get_u32_le(page + 8) != CF_KEY_SEALED_VERSION ||
        get_u32_le(page + 132) != CF_KEY_COMMIT_MARKER ||
        get_u32_le(page + 128) != cf_key_crc32(page, 128)) {
        return;
    }
    const uint32_t generation = get_u32_le(page + 12);
    const uint32_t iterations = get_u32_le(page + 16);
    if (generation == 0 || iterations < CF_SEAL_MIN_ITERATIONS ||
        iterations > CF_SEAL_MAX_ITERATIONS) {
        return;
    }
    slot->valid = 1;
    slot->sealed = 1;
    slot->generation = generation;
}

/* Picks the record to use: 0 page A, 1 page B, -1 none, -2 two different records claim the
 * same generation (corruption: never guess between identities). */
static int pick_winner(const uint8_t *page_a, const page_slot *a, const uint8_t *page_b,
                       const page_slot *b) {
    if (a->valid && b->valid) {
        if (a->generation != b->generation) {
            return a->generation > b->generation ? 0 : 1;
        }
        if (!a->sealed && !b->sealed) {
            return (memcmp(a->keys.key1, b->keys.key1, 32) == 0 &&
                    memcmp(a->keys.key2, b->keys.key2, 32) == 0) ? 0 : -2;
        }
        if (a->sealed && b->sealed) {
            return memcmp(page_a, page_b, CF_KEY_SEALED_SIZE) == 0 ? 0 : -2;
        }
        return -2;   /* a plain and a sealed record cannot share a generation */
    }
    if (a->valid) {
        return 0;
    }
    if (b->valid) {
        return 1;
    }
    return -1;
}

static cf_key_status empty_or_corrupt(const cf_key_flash *flash, const uint8_t *page_a,
                                      const uint8_t *page_b) {
    const int erased_a = all_bytes(page_a, CF_KEY_PAGE_SIZE, 0xFF);
    const int erased_b = all_bytes(page_b, CF_KEY_PAGE_SIZE, 0xFF);
    const int database_erased = flash->database_erased(flash->ctx);
    if (database_erased < 0) {
        return CF_KEY_IO_ERROR;
    }
    if (erased_a && erased_b && database_erased == 1) {
        return CF_KEY_EMPTY;
    }
    return CF_KEY_CORRUPT;
}

static int flash_ok(const cf_key_flash *flash) {
    return flash != NULL && flash->read != NULL && flash->database_erased != NULL &&
           flash->program != NULL && flash->erase != NULL;
}

cf_key_status cf_key_pages_status(const uint8_t page_a[CF_KEY_PAGE_SIZE],
                                  const uint8_t page_b[CF_KEY_PAGE_SIZE]) {
    page_slot a;
    page_slot b;
    if (page_a == NULL || page_b == NULL) {
        return CF_KEY_INVALID_ARGUMENT;
    }
    decode_slot(page_a, &a);
    decode_slot(page_b, &b);
    const int winner = pick_winner(page_a, &a, page_b, &b);
    const cf_key_status status =
        winner < 0 ? CF_KEY_CORRUPT
                   : ((winner == 0 ? a.sealed : b.sealed) ? CF_KEY_LOCKED : CF_KEY_READY);
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    return status;
}

cf_key_status cf_key_store_load(const cf_key_flash *flash, cf_device_keys *out) {
    uint8_t page_a[CF_KEY_PAGE_SIZE];
    uint8_t page_b[CF_KEY_PAGE_SIZE];
    page_slot a;
    page_slot b;

    if (!flash_ok(flash) || out == NULL) {
        return CF_KEY_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    if (read_page(flash, 0, page_a) != CF_KEY_READY ||
        read_page(flash, CF_KEY_PAGE_SIZE, page_b) != CF_KEY_READY) {
        return CF_KEY_IO_ERROR;
    }
    decode_slot(page_a, &a);
    decode_slot(page_b, &b);
    const int winner = pick_winner(page_a, &a, page_b, &b);
    cf_key_status status;
    if (winner == -2) {
        status = CF_KEY_CORRUPT;
    } else if (winner == -1) {
        status = empty_or_corrupt(flash, page_a, page_b);
    } else {
        const page_slot *w = winner == 0 ? &a : &b;
        if (w->sealed) {
            status = CF_KEY_LOCKED;
        } else {
            *out = w->keys;
            status = CF_KEY_READY;
        }
    }
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(page_a, 0, sizeof(page_a));
    memset(page_b, 0, sizeof(page_b));
    return status;
}

static int fill_random(const cf_key_flash *flash, uint8_t *out, size_t len) {
    if (flash->random == NULL) {
        return -1;
    }
    return flash->random(flash->ctx, out, len);
}

static int generate_keys(const cf_key_flash *flash, cf_device_keys *keys) {
    memset(keys, 0, sizeof(*keys));
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (fill_random(flash, keys->key1, sizeof(keys->key1)) != 0) {
            return -1;
        }
        if (!all_bytes(keys->key1, sizeof(keys->key1), 0x00) &&
            !all_bytes(keys->key1, sizeof(keys->key1), 0xFF)) {
            break;
        }
        if (attempt == 63) {
            return -1;
        }
    }
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (fill_random(flash, keys->key2, sizeof(keys->key2)) != 0) {
            return -1;
        }
        if (scalar_valid(keys->key2)) {
            return 0;
        }
    }
    return -1;
}

static cf_key_status write_page(const cf_key_flash *flash, uint32_t offset,
                                const cf_device_keys *keys,
                                uint8_t record[CF_KEY_PAGE_SIZE]) {
    cf_key_record_encode(keys, record);
    if (flash->erase(flash->ctx, offset, CF_KEY_PAGE_SIZE) != 0) {
        return CF_KEY_IO_ERROR;
    }
    /* Commit marker is written only after the payload and CRC are durable. */
    if (flash->program(flash->ctx, offset, record, 84) != 0) {
        return CF_KEY_IO_ERROR;
    }
    if (flash->program(flash->ctx, offset + 84, record + 84, 4) != 0) {
        return CF_KEY_IO_ERROR;
    }
    if (flash->read(flash->ctx, offset, record, CF_KEY_PAGE_SIZE) != 0) {
        return CF_KEY_IO_ERROR;
    }
    cf_device_keys verify;
    memset(&verify, 0, sizeof(verify));
    const cf_key_status status = cf_key_record_decode(record, &verify);
    const int equal = status == CF_KEY_READY &&
                      memcmp(verify.key1, keys->key1, 32) == 0 &&
                      memcmp(verify.key2, keys->key2, 32) == 0;
    memset(&verify, 0, sizeof(verify));
    return equal ? CF_KEY_READY : CF_KEY_CORRUPT;
}

cf_key_status cf_key_store_provision(const cf_key_flash *flash,
                                     cf_device_keys *out) {
    uint8_t record[CF_KEY_PAGE_SIZE];
    cf_device_keys existing;
    cf_device_keys generated;
    cf_key_status status;

    if (flash == NULL || out == NULL || flash->read == NULL ||
        flash->program == NULL || flash->erase == NULL ||
        flash->database_erased == NULL) {
        return CF_KEY_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    status = cf_key_store_load(flash, &existing);
    if (status == CF_KEY_READY) {
        *out = existing;
        memset(&existing, 0, sizeof(existing));
        return CF_KEY_READY;
    }
    memset(&existing, 0, sizeof(existing));
    if (status != CF_KEY_EMPTY) {
        return status;   /* LOCKED, CORRUPT and errors never lead to a new identity */
    }
    if (generate_keys(flash, &generated) != 0) {
        memset(&generated, 0, sizeof(generated));
        return CF_KEY_IO_ERROR;
    }

    status = write_page(flash, 0, &generated, record);
    if (status != CF_KEY_READY) {
        memset(&generated, 0, sizeof(generated));
        memset(record, 0, sizeof(record));
        return status;
    }
    /* A committed page A is already a durable identity. A failed mirror is
     * reported by the next load as a single valid record, not as data loss. */
    (void)write_page(flash, CF_KEY_PAGE_SIZE, &generated, record);

    *out = generated;
    memset(&generated, 0, sizeof(generated));
    memset(record, 0, sizeof(record));
    return CF_KEY_READY;
}

/* ---------------------------------------------------------------------------------------
 * Sealed records. Copyright (c) 2026 Koustav Kumar Mondal.
 * ------------------------------------------------------------------------------------- */

static cf_key_status write_raw(const cf_key_flash *flash, uint32_t offset,
                               const uint8_t *record, size_t len) {
    uint8_t check[CF_KEY_PAGE_SIZE];
    if (flash->erase(flash->ctx, offset, CF_KEY_PAGE_SIZE) != 0) {
        return CF_KEY_IO_ERROR;
    }
    /* The commit marker (last 4 bytes) goes in only after everything before it. */
    if (flash->program(flash->ctx, offset, record, len - 4) != 0 ||
        flash->program(flash->ctx, offset + (uint32_t)len - 4, record + len - 4, 4) != 0) {
        return CF_KEY_IO_ERROR;
    }
    if (flash->read(flash->ctx, offset, check, CF_KEY_PAGE_SIZE) != 0) {
        return CF_KEY_IO_ERROR;
    }
    const int same = memcmp(check, record, len) == 0 &&
                     all_bytes(check + len, CF_KEY_PAGE_SIZE - len, 0xFF);
    memset(check, 0, sizeof(check));
    return same ? CF_KEY_READY : CF_KEY_CORRUPT;
}

static cf_key_status open_store(const cf_key_flash *flash, const uint8_t *pass, size_t pass_len,
                                uint8_t page_a[CF_KEY_PAGE_SIZE], uint8_t page_b[CF_KEY_PAGE_SIZE],
                                int *winner_out, uint32_t *max_generation,
                                cf_device_keys *out) {
    page_slot a;
    page_slot b;
    memset(out, 0, sizeof(*out));
    if (read_page(flash, 0, page_a) != CF_KEY_READY ||
        read_page(flash, CF_KEY_PAGE_SIZE, page_b) != CF_KEY_READY) {
        return CF_KEY_IO_ERROR;
    }
    decode_slot(page_a, &a);
    decode_slot(page_b, &b);
    const int winner = pick_winner(page_a, &a, page_b, &b);
    *winner_out = winner;
    *max_generation = (a.valid && a.generation > b.generation) ? a.generation
                      : (b.valid ? b.generation : 0);
    if (a.valid && a.generation > *max_generation) {
        *max_generation = a.generation;
    }
    cf_key_status status;
    if (winner == -2) {
        status = CF_KEY_CORRUPT;
    } else if (winner == -1) {
        status = empty_or_corrupt(flash, page_a, page_b);
    } else {
        const page_slot *w = winner == 0 ? &a : &b;
        const uint8_t *page = winner == 0 ? page_a : page_b;
        if (!w->sealed) {
            *out = w->keys;
            status = CF_KEY_READY;
        } else {
            uint8_t plain[64];
            const int rc = cf_seal_decrypt(pass, pass_len, page + 20, get_u32_le(page + 16),
                                           page + 36, page, 48, page + 48, page + 112, plain);
            if (rc == 0) {
                memcpy(out->key1, plain, 32);
                memcpy(out->key2, plain + 32, 32);
                status = keys_valid(out) ? CF_KEY_READY : CF_KEY_CORRUPT;
            } else {
                status = (rc == 1 || pass == NULL || pass_len == 0) ? CF_KEY_WRONG_PASSPHRASE
                                                                    : CF_KEY_IO_ERROR;
            }
            memset(plain, 0, sizeof(plain));
        }
    }
    if (status != CF_KEY_READY) {
        memset(out, 0, sizeof(*out));
    }
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    return status;
}

cf_key_status cf_key_store_unlock(const cf_key_flash *flash, const uint8_t *pass,
                                  size_t pass_len, cf_device_keys *out) {
    uint8_t page_a[CF_KEY_PAGE_SIZE];
    uint8_t page_b[CF_KEY_PAGE_SIZE];
    int winner = -1;
    uint32_t max_generation = 0;
    if (!flash_ok(flash) || out == NULL) {
        return CF_KEY_INVALID_ARGUMENT;
    }
    cf_key_status status = open_store(flash, pass, pass_len, page_a, page_b, &winner,
                                      &max_generation, out);
    if (status == CF_KEY_READY && winner >= 0) {
        const uint8_t *w = winner == 0 ? page_a : page_b;
        uint8_t *other = winner == 0 ? page_b : page_a;
        if (memcmp(w, kSealedMagic, sizeof(kSealedMagic)) == 0 &&
            memcmp(w, other, CF_KEY_PAGE_SIZE) != 0) {
            /* Scrub a stale or interrupted copy (possibly plaintext). Best effort: the
             * winner is already durable, a failure here is repaired next time. */
            (void)write_raw(flash, winner == 0 ? CF_KEY_PAGE_SIZE : 0, w, CF_KEY_SEALED_SIZE);
        }
    }
    memset(page_a, 0, sizeof(page_a));
    memset(page_b, 0, sizeof(page_b));
    return status;
}

cf_key_status cf_key_store_change_passphrase(const cf_key_flash *flash,
                                             const uint8_t *old_pass, size_t old_len,
                                             const uint8_t *new_pass, size_t new_len,
                                             uint32_t iterations) {
    uint8_t page_a[CF_KEY_PAGE_SIZE];
    uint8_t page_b[CF_KEY_PAGE_SIZE];
    uint8_t record[CF_KEY_PAGE_SIZE];
    cf_device_keys keys;
    int winner = -1;
    uint32_t max_generation = 0;
    if (!flash_ok(flash) || (new_len > 0 && new_pass == NULL) ||
        (new_len > 0 && (iterations < CF_SEAL_MIN_ITERATIONS ||
                         iterations > CF_SEAL_MAX_ITERATIONS))) {
        return CF_KEY_INVALID_ARGUMENT;
    }
    cf_key_status status = open_store(flash, old_pass, old_len, page_a, page_b, &winner,
                                      &max_generation, &keys);
    if (status != CF_KEY_READY) {
        memset(page_a, 0, sizeof(page_a));
        memset(page_b, 0, sizeof(page_b));
        return status;
    }
    size_t len;
    if (new_len == 0) {
        cf_key_record_encode(&keys, record);
        len = CF_KEY_RECORD_SIZE;
    } else {
        memset(record, 0xFF, sizeof(record));
        memcpy(record, kSealedMagic, sizeof(kSealedMagic));
        put_u32_le(record + 8, CF_KEY_SEALED_VERSION);
        put_u32_le(record + 12, max_generation + 1);
        put_u32_le(record + 16, iterations);
        uint8_t plain[64];
        memcpy(plain, keys.key1, 32);
        memcpy(plain + 32, keys.key2, 32);
        const int ok = fill_random(flash, record + 20, CF_SEAL_SALT_SIZE) == 0 &&
                       fill_random(flash, record + 36, CF_SEAL_NONCE_SIZE) == 0 &&
                       cf_seal_encrypt(new_pass, new_len, record + 20, iterations, record + 36,
                                       record, 48, plain, record + 48, record + 112) == 0;
        memset(plain, 0, sizeof(plain));
        if (!ok) {
            memset(&keys, 0, sizeof(keys));
            memset(record, 0, sizeof(record));
            memset(page_a, 0, sizeof(page_a));
            memset(page_b, 0, sizeof(page_b));
            return CF_KEY_IO_ERROR;
        }
        put_u32_le(record + 128, cf_key_crc32(record, 128));
        put_u32_le(record + 132, CF_KEY_COMMIT_MARKER);
        len = CF_KEY_SEALED_SIZE;
    }
    /* Write the page that does not hold the current winner first: if power fails, the
     * winner still opens with the old passphrase. Then overwrite the old winner. */
    const uint32_t first = winner == 1 ? 0 : CF_KEY_PAGE_SIZE;
    const uint32_t second = first == 0 ? CF_KEY_PAGE_SIZE : 0;
    status = write_raw(flash, first, record, len);
    if (status == CF_KEY_READY) {
        status = write_raw(flash, second, record, len);
    }
    memset(&keys, 0, sizeof(keys));
    memset(record, 0, sizeof(record));
    memset(page_a, 0, sizeof(page_a));
    memset(page_b, 0, sizeof(page_b));
    return status;
}
