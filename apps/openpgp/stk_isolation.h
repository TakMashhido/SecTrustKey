/*
 * SecTrustKey: keeps the destructive commands of one application away from the
 * files of the others.
 *
 * FIDO, OpenPGP and PIV share one flat file table and one dynamic-file store.
 * OpenPGP TERMINATE DF and PIV RESET were written for a card that held only
 * their own data, so they walk everything. These helpers say which application a
 * FID belongs to so those commands can be limited to their own files.
 *
 * Ownership is not guessed: the OpenPGP/PIV FIDs are exactly the entries that
 * came from the upstream OpenPGP table (merged into fido/files.c) plus the
 * key-container FID range
 * (0xF0xx-0xF5xx). OpenPGP and PIV create no other dynamic files.
 *
 * Copyright (c) 2026 Koustav Kumar Mondal
 *
 * This file is part of SecTrustKey and is licensed under the GNU Affero General
 * Public License, version 3, like the rest of the project.
 */

#ifndef _STK_ISOLATION_H_
#define _STK_ISOLATION_H_

#include <stdbool.h>
#include <stdint.h>

/* Generated into apps/fido/files.c: FIDs that came from the OpenPGP/PIV table. */
extern bool stk_openpgp_table_owns_fid(uint16_t fid);

/* FID belongs to OpenPGP or PIV (static table entry or key container). */
extern bool stk_fid_in_openpgp_domain(uint16_t fid);

/* FID belongs to PIV: its PINs, DEK, certificate objects and key slots. */
extern bool stk_fid_is_piv(uint16_t fid);

/* PIV RESET: remove PIV state only, leave OpenPGP and FIDO untouched, and restore
 * the PIV retry counters inside the files PIV shares with OpenPGP. The caller
 * re-runs PIV initialisation afterwards to recreate the default PIN/PUK/key. */
extern int stk_piv_reset_storage(void);

#endif /* _STK_ISOLATION_H_ */
