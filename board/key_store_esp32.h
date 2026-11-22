#ifndef CF_KEY_STORE_ESP32_H
#define CF_KEY_STORE_ESP32_H

#include <stdint.h>

#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the validated key store (key pages at offset 0, filesystem at
 * CF_FIDO_FS_OFFSET), or NULL before init/failure. For an stk_store partition this is a
 * view that starts after the installer pad. */
const esp_partition_t *cardputer_fido_partition(void);
/* CF_STORE_FIXED (fido_store) or CF_STORE_INSTALLER (stk_store); -1 before prepare. */
int cardputer_key_store_kind(void);
/* 1 when neither key page holds any SecTrustKey record: a never-initialised partition. */
int cardputer_key_store_foreign(void);
/* 0 when keys are loaded or provisioned, negative on any failure. */
int cardputer_key_store_init(void);
int cardputer_key_store_status(void);
int cardputer_key_store_sealed(void);
/* Validate/discover the partition without loading or provisioning secrets. */
int cardputer_key_store_prepare(void);

#ifdef __cplusplus
}
#endif

#endif
