/*
 * SecTrustKey: per-application ownership of the shared file store. See
 * stk_isolation.h for why this exists.
 *
 * Copyright (c) 2026 Koustav Kumar Mondal
 *
 * This file is part of SecTrustKey and is licensed under the GNU Affero General
 * Public License, version 3, like the rest of the project.
 */

#include "stk_isolation.h"
#include "openpgp.h"
#include "key_container.h"
#include "openpgp_vault.h"
#include "piv.h"

/* PW status (EF_PW_PRIV) is { fmt, max-len x3, retries: PW1 RC PW3 PIV-PIN PIV-PUK }.
 * PW retries (EF_PW_RETRIES) is { fmt, max: PW1 RC PW3 PIV-PIN PIV-PUK }. Both files
 * are shared with OpenPGP: only the PIV bytes may be touched here. */
#define STK_PW_PRIV_SIZE 9u
#define STK_PW_PRIV_PIV_PIN 7u
#define STK_PW_PRIV_PIV_PUK 8u
#define STK_PW_RETRIES_SIZE 6u
#define STK_PW_RETRIES_PIV_PIN 4u
#define STK_PW_RETRIES_PIV_PUK 5u
#define STK_PIV_DEFAULT_RETRIES 3u

bool stk_fid_in_openpgp_domain(uint16_t fid) {
    return stk_openpgp_table_owns_fid(fid) || openpgp_key_container_prefix_fid(fid);
}

bool stk_fid_is_piv(uint16_t fid) {
    if (openpgp_key_container_prefix_fid(fid)) {
        /* container manifest/record: the low byte is the container id */
        return openpgp_key_container_is_piv(fid & UINT8_MAX);
    }
    if (openpgp_key_container_is_piv(fid)) {
        return true; /* logical key slot marker */
    }
    return fid == EF_PIV_PIN || fid == EF_PIV_PUK || fid == EF_DEK_PWPIV ||
           fid == EF_PIV_DISCOVERY || fid == EF_PIV_BITGT ||
           (fid >= EF_PIV_ADMIN_DATA && fid <= EF_PIV_MSROOTS5) ||
           (fid >= EF_PIV_CARD_AUTH && fid <= EF_PIV_PC_REF_DATA);
}

typedef struct stk_reset_context {
    int value;
    int metadata;
} stk_reset_context_t;

static int stk_delete_if_piv(file_t *file, stk_reset_context_t *context) {
    if (file->fid == 0 || !stk_fid_is_piv(file->fid)) {
        return STK_OK;
    }
    file_delete_result_t result = file_delete_no_commit_parts(file);
    if (context->metadata == STK_OK && result.metadata != STK_OK) {
        context->metadata = result.metadata;
    }
    return result.value;
}

static bool stk_delete_dynamic_piv(file_t *file, void *ctx) {
    stk_reset_context_t *context = (stk_reset_context_t *)ctx;
    int ret = stk_delete_if_piv(file, context);
    if (ret != STK_OK) {
        context->value = ret;
        return false;
    }
    return true;
}

/* Restore only the PIV bytes of a file shared with OpenPGP. An empty or short file
 * is left for scan_files_piv() to initialise: it already handles those cases. */
static int stk_reset_piv_counter_file(uint16_t fid, uint8_t size, uint8_t pin_index, uint8_t puk_index) {
    file_t *ef = file_search_by_fid(fid, NULL, SPECIFY_ANY);
    if (!ef || !file_has_data(ef) || file_get_size(ef) < size) {
        return STK_OK;
    }
    uint8_t buffer[STK_PW_PRIV_SIZE];
    memcpy(buffer, file_get_data(ef), size);
    buffer[pin_index] = STK_PIV_DEFAULT_RETRIES;
    buffer[puk_index] = STK_PIV_DEFAULT_RETRIES;
    return file_put_data(ef, CONST_BYTE_ARRAY(buffer, size));
}

int stk_piv_reset_storage(void) {
    stk_reset_context_t context = { .value = STK_OK, .metadata = STK_OK };

    int ret = openpgp_vault_clear_piv();
    if (ret != STK_OK) {
        return ret;
    }
    for (file_entry_t *entry = file_entries; entry != file_last; entry++) {
        if (!(file_get_type(&entry->file) & FILE_DATA_FLASH)) {
            continue;
        }
        ret = stk_delete_if_piv(&entry->file, &context);
        if (ret != STK_OK) {
            return ret;
        }
    }
    file_for_each_dynamic(stk_delete_dynamic_piv, &context);
    if (context.value != STK_OK) {
        return context.value;
    }
    /* Key metadata is keyed by the logical slot FID, not by the container records. */
    for (uint16_t slot = 0; slot <= UINT8_MAX; slot++) {
        if (openpgp_key_container_is_piv(slot)) {
            (void)meta_delete_no_commit(slot);
        }
    }
    ret = stk_reset_piv_counter_file(EF_PW_PRIV, STK_PW_PRIV_SIZE, STK_PW_PRIV_PIV_PIN, STK_PW_PRIV_PIV_PUK);
    if (ret != STK_OK) {
        return ret;
    }
    ret = stk_reset_piv_counter_file(EF_PW_RETRIES, STK_PW_RETRIES_SIZE, STK_PW_RETRIES_PIV_PIN, STK_PW_RETRIES_PIV_PUK);
    if (ret != STK_OK) {
        return ret;
    }
    flash_commit();
    file_initialize_flash(false);
    return context.metadata;
}
