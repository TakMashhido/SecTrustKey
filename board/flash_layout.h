#ifndef CF_FLASH_LAYOUT_H
#define CF_FLASH_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#define CF_FIDO_PART_TYPE 0x40u
#define CF_FIDO_PART_SUBTYPE 0x01u
#define CF_FIDO_PART_LABEL "fido_store"
#define CF_FIDO_STORE_SIZE 0x102000u
#define CF_FIDO_KEYS_OFFSET 0x0u
#define CF_FIDO_FS_OFFSET 0x2000u
#define CF_FIDO_FS_SIZE 0x100000u
#define CF_FLASH_RESERVED_SIZE 0x9000u
#define CF_MAX_PARTITIONS 32u

/*
 * The installer-friendly layout. Firmware launchers such as M5 Launcher only create
 * data partitions they know (FAT, SPIFFS, LittleFS) and copy whatever bytes an image
 * carries for them on every install and update. So the store is also accepted as a
 * SPIFFS-typed data partition whose first sector is an "installer pad": the image ships
 * 4 KiB there, an installer rewrites only that sector, and the key pages and filesystem
 * start right after it. The contents are not a SPIFFS volume.
 */
#define CF_STK_PART_TYPE 0x01u
#define CF_STK_PART_SUBTYPE 0x82u
#define CF_STK_PART_LABEL "stk_store"
#define CF_STK_PAD_SIZE 0x1000u
#define CF_STK_PART_SIZE (CF_STK_PAD_SIZE + CF_FIDO_STORE_SIZE)

typedef enum {
    CF_STORE_FIXED = 0,    /* fido_store, type 0x40: full-flash installs up to v1.0 */
    CF_STORE_INSTALLER = 1 /* stk_store, data/SPIFFS with an installer pad */
} cf_store_kind;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CF_LAYOUT_OK = 0,
    CF_LAYOUT_MISSING = 1,
    CF_LAYOUT_WRONG_TYPE = 2,
    CF_LAYOUT_WRONG_SIZE = 3,
    CF_LAYOUT_NOT_ALIGNED = 4,
    CF_LAYOUT_OUT_OF_RANGE = 5,
    CF_LAYOUT_OVERLAP = 6,
    CF_LAYOUT_DUPLICATE_LABEL = 7,
    CF_LAYOUT_TOO_MANY = 8,
    CF_LAYOUT_INVALID_ARGUMENT = 9
} cf_layout_status;

typedef struct {
    uint8_t type;
    uint8_t subtype;
    char label[17];
    uint32_t offset;
    uint32_t size;
} cf_partition_desc;

typedef struct {
    uint32_t flash_size;
    uint32_t store_offset;     /* absolute address of the key pages */
    uint32_t partition_offset; /* absolute address of the partition that holds them */
    cf_store_kind kind;
} cf_flash_layout;

/*
 * Validates an injected partition descriptor set. No partition table is
 * written and no fixed physical address is assumed. A fido_store partition is
 * preferred when present; otherwise an stk_store partition is used.
 */
cf_layout_status cf_flash_layout_validate(const cf_partition_desc *partitions,
                                           size_t count, uint32_t flash_size,
                                           cf_flash_layout *out);

/*
 * Translates a logical platform filesystem offset into the physical address
 * inside fido_store. The two key pages are never reachable through this
 * function.
 */
int cf_flash_layout_physical(const cf_flash_layout *layout, uint32_t logical,
                             size_t len, uint32_t *physical);

#ifdef __cplusplus
}
#endif

#endif
