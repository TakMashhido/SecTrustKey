#include "key_store_esp32.h"

#include <string.h>

#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "flash_layout.h"
#include "key_store.h"
#include "otp_platform.h"
#include "stk.h"

static const esp_partition_t *fido_partition = NULL;
static cf_flash_layout fido_layout;
static cf_device_keys device_keys;
static int store_status = -1;
static int initialized = 0;

const esp_partition_t *cardputer_fido_partition(void) {
    return fido_partition;
}

int cardputer_key_store_status(void) {
    return store_status;
}

static int store_read(void *ctx, uint32_t offset, uint8_t *out, size_t len) {
    (void)ctx;
    if (fido_partition == NULL) {
        return -1;
    }
    return esp_partition_read(fido_partition, offset, out, len) == ESP_OK ? 0 : -1;
}

static int store_program(void *ctx, uint32_t offset, const uint8_t *data,
                         size_t len) {
    (void)ctx;
    if (fido_partition == NULL) {
        return -1;
    }
    return esp_partition_write(fido_partition, offset, data, len) == ESP_OK ? 0 : -1;
}

static int store_erase(void *ctx, uint32_t offset, size_t len) {
    (void)ctx;
    if (fido_partition == NULL) {
        return -1;
    }
    return esp_partition_erase_range(fido_partition, offset, len) == ESP_OK ? 0 : -1;
}

static int database_erased(void *ctx) {
    (void)ctx;
    uint8_t buffer[512];
    for (uint32_t offset = CF_FIDO_FS_OFFSET; offset < CF_FIDO_STORE_SIZE;
         offset += sizeof(buffer)) {
        const size_t length =
            (CF_FIDO_STORE_SIZE - offset) < sizeof(buffer)
                ? (CF_FIDO_STORE_SIZE - offset)
                : sizeof(buffer);
        if (store_read(NULL, offset, buffer, length) != 0) {
            return -1;
        }
        for (size_t i = 0; i < length; ++i) {
            if (buffer[i] != 0xFF) {
                return 0;
            }
        }
    }
    return 1;
}

static int random_bytes(void *ctx, uint8_t *out, size_t len) {
    (void)ctx;
    esp_fill_random(out, len);
    return 0;
}

static int validate_partition_table(void) {
    cf_partition_desc descriptors[CF_MAX_PARTITIONS];
    size_t count = 0;
    esp_partition_iterator_t iterator =
        esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (iterator != NULL && count < CF_MAX_PARTITIONS) {
        const esp_partition_t *partition = esp_partition_get(iterator);
        if (partition != NULL) {
            memset(&descriptors[count], 0, sizeof(descriptors[count]));
            descriptors[count].type = (uint8_t)partition->type;
            descriptors[count].subtype = (uint8_t)partition->subtype;
            descriptors[count].offset = partition->address;
            descriptors[count].size = partition->size;
            strncpy(descriptors[count].label, partition->label,
                    sizeof(descriptors[count].label) - 1);
            ++count;
        }
        iterator = esp_partition_next(iterator);
    }
    const bool too_many = iterator != NULL;
    esp_partition_iterator_release(iterator);
    if (too_many) {
        return -(int)CF_LAYOUT_TOO_MANY;
    }
    uint32_t flash_size = 0;
    if (esp_flash_get_size(esp_flash_default_chip, &flash_size) != ESP_OK) {
        return -(int)CF_LAYOUT_INVALID_ARGUMENT;
    }
    const cf_layout_status status =
        cf_flash_layout_validate(descriptors, count, flash_size, &fido_layout);
    if (status != CF_LAYOUT_OK) {
        return -(int)status;
    }
    return 0;
}

/* The store as every caller sees it: the key pages at offset 0 and the filesystem at
 * CF_FIDO_FS_OFFSET. For a fido_store partition this is the partition itself; for an
 * stk_store partition it starts one sector in, after the installer pad. Only address and
 * size differ from the real partition, and esp_partition_read/write/erase_range/mmap use
 * just those fields, so the installer pad can never be read or written through it. */
static esp_partition_t store_view;

/* After validate_partition_table(): the view of the partition it selected, or NULL. */
static const esp_partition_t *open_store_view(void) {
    const int fixed = fido_layout.kind == CF_STORE_FIXED;
    const esp_partition_t *partition = esp_partition_find_first(
        (esp_partition_type_t)(fixed ? CF_FIDO_PART_TYPE : CF_STK_PART_TYPE),
        (esp_partition_subtype_t)(fixed ? CF_FIDO_PART_SUBTYPE : CF_STK_PART_SUBTYPE),
        fixed ? CF_FIDO_PART_LABEL : CF_STK_PART_LABEL);
    if (partition == NULL || partition->address != fido_layout.partition_offset) {
        return NULL;
    }
    store_view = *partition;
    store_view.address = fido_layout.store_offset;
    store_view.size = CF_FIDO_STORE_SIZE;
    return &store_view;
}

int cardputer_key_store_prepare(void) {
    fido_partition = NULL;
    const int validation = validate_partition_table();
    if (validation != 0) {
        store_status = validation;
        return store_status;
    }
    fido_partition = open_store_view();
    if (fido_partition == NULL) {
        store_status = -(int)CF_LAYOUT_MISSING;
        return store_status;
    }
    return 0;
}

/* Provided by the board UI (backup.cpp via ui.cpp): shows the lock screen and returns 0
 * with the unlocked keys. The weak default fails closed: without a lock screen a sealed
 * store stays locked and USB stays off. */
__attribute__((weak)) int cardputer_unlock_device(const cf_key_flash *flash, cf_device_keys *out) {
    (void)flash;
    memset(out, 0, sizeof(*out));
    return -1;
}

static int sealed_at_boot = 0;
/* Dashboard: was the store passphrase-sealed when it was opened at boot? */
int cardputer_key_store_sealed(void) {
    return sealed_at_boot;
}

int cardputer_key_store_init(void) {
    if (initialized) {
        return store_status;
    }
    initialized = 1;
    if (cardputer_key_store_prepare() != 0) {
        return store_status;
    }

    cf_key_flash flash;
    flash.ctx = NULL;
    flash.read = store_read;
    flash.program = store_program;
    flash.erase = store_erase;
    flash.database_erased = database_erased;
    flash.random = random_bytes;

    cf_key_status status = cf_key_store_load(&flash, &device_keys);
    if (status == CF_KEY_LOCKED) {
        sealed_at_boot = 1;
        /* Passphrase-sealed: the board shows the lock screen (USB is not started yet) and
         * returns only with the unlocked keys. */
        status = cardputer_unlock_device(&flash, &device_keys) == 0 ? CF_KEY_READY : CF_KEY_LOCKED;
    }
    if (status == CF_KEY_EMPTY) {
        /* random_init() already enabled the shared entropy source. Leave it
         * running for subsequent protocol key generation and signatures. */
        status = cf_key_store_provision(&flash, &device_keys);
    }
    if (status != CF_KEY_READY) {
        memset(&device_keys, 0, sizeof(device_keys));
        store_status = status == CF_KEY_CORRUPT ? -3 : -2;
        return store_status;
    }
    store_status = 0;
    return store_status;
}

void otp_platform_init(const uint8_t **otp_key_1_out,
                       const uint8_t **otp_key_2_out) {
    if (otp_key_1_out != NULL) {
        *otp_key_1_out = NULL;
    }
    if (otp_key_2_out != NULL) {
        *otp_key_2_out = NULL;
    }
    if (cardputer_key_store_init() != 0) {
        return;
    }
    if (otp_key_1_out != NULL) {
        *otp_key_1_out = device_keys.key1;
    }
    if (otp_key_2_out != NULL) {
        *otp_key_2_out = device_keys.key2;
    }
}

int cardputer_key_store_kind(void) {
    return fido_partition == NULL ? -1 : (int)fido_layout.kind;
}

/* 1 when neither key page has ever held a SecTrustKey record (no CFKEY magic at all), as in
 * a partition an installer has just created over another app's old data. Used only to word
 * the recovery screen; erasing always needs the user's explicit confirmation. */
int cardputer_key_store_foreign(void) {
    static const uint8_t prefix[7] = {'C', 'F', 'K', 'E', 'Y', '0', '0'};
    uint8_t head[sizeof(prefix)];
    for (uint32_t page = 0; page < 2; ++page) {
        if (store_read(NULL, page * 0x1000u, head, sizeof(head)) != 0) {
            return 0;
        }
        if (memcmp(head, prefix, sizeof(prefix)) == 0) {
            return 0;
        }
    }
    return 1;
}

int otp_platform_enable_secure_boot(uint8_t bootkey, bool secure_lock) {
    (void)bootkey;
    (void)secure_lock;
    /* Provisioning security features is deliberately unreachable on this
     * port. Return an error instead of delegating to the eFuse backend. */
    return -1;
}

bool otp_platform_is_secure_boot_enabled(uint8_t *bootkey) {
    (void)bootkey;
    return false;
}

bool otp_platform_is_secure_boot_locked(void) {
    return false;
}
