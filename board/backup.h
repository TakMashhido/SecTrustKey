#pragma once
#include "board.h"
#include "key_store.h"

namespace cardputer {
enum class BackupMode : unsigned char { None = 0, Save = 1, Restore = 2, Passphrase = 3, Verify = 4, SdInfo = 5 };
/* Requests are persisted by a worker after flushing SDK storage, then reboot.
 * Passwords are entered only in the pre-USB maintenance boot, never persisted. */
bool request_backup_mode(BackupMode mode);
/* True once an SD backup was saved and verified on this device (for the dashboard). */
bool backup_verified_once();
void backup_startup(Board& board);
/* Boot with a passphrase-sealed key store: shows the lock screen before USB starts and
 * returns only once `out` holds the unlocked keys. Wrong tries only add delays. */
void unlock_store_at_boot(Board& board, const cf_key_flash* flash, cf_device_keys* out);
/* Fatal storage errors expose restore only, without starting USB/CTAP. */
[[noreturn]] void backup_recovery(Board& board);
}  // namespace cardputer
