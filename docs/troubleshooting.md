---
title: Troubleshooting
description: Fix common SecTrustKey problems — flashing and download mode, gpg not finding the card, hanging requests, Bad PIN errors, storage errors and missing TOTP codes.
---

# Troubleshooting

## Flashing

??? question "esptool says *No serial data received* or cannot connect"
    The Cardputer is running its firmware instead of the ROM loader. Power it off, hold **G0**, plug in USB, release **G0**, and try again. Check the cable carries data: `lsusb` should list `303a:1001`.

??? question "The screen stays dark after flashing"
    The chip is still in the loader. Add `--after watchdog_reset` to the esptool command, or switch the device off and on without holding G0.

??? question "The first start asks *Set up storage?*"
    The storage area holds another app's old data, which is normal after installing from M5 Launcher or flashing without `--erase-all`. Press ++enter++ to prepare it. SecTrustKey only offers this for an area that has never held SecTrustKey keys.

??? question "The device shows *Storage recovery* and USB stays off"
    The key store was found damaged. Restore an [SD backup](guide/backup.md) from the recovery screen. If you have none, decline the restore: the device then offers to erase and start over, after two confirmations, and all keys in it are lost.

??? question "*Storage layout error* after using M5 Launcher"
    The key storage partition `stk_store` is gone, usually because a SecTrustKey entry was deleted in Launcher, which removes the storage with it. Install SecTrustKey again from Launcher; if Launcher offers **Restore Data** from its own backup, accept it, otherwise restore your SecTrustKey [SD backup](guide/backup.md).

## GnuPG and smart cards

??? question "`gpg --card-status` finds no card"
    1. On Linux, add SecTrustKey's ID to the [CCID driver list](guide/openpgp.md#linux-let-pcscd-see-the-reader) and restart `pcscd`.
    2. Only one program can own the reader. `gpgconf --kill scdaemon` releases GnuPG's hold; close `ykman` sessions and browser smart-card prompts.
    3. `pcsc_scan -r` should list *SecTrustKey*.

??? question "`gpg` says *Bad PIN* when changing a PIN"
    If the retry counter did not drop, the new PIN was too short: 6+ characters for the user PIN, 8+ for the admin PIN. See [Change the PINs](guide/openpgp.md#change-the-pins).

## FIDO and browsers

??? question "A request seems to hang"
    The device is waiting for ++enter++ or ++backspace++; look at its screen. Avoid FIDO requests while a long smart-card command, such as RSA key generation, is running.

??? question "The browser asks for a PIN you never set"
    Another program or browser tab has an open security-key request. Cancel it and try again.

??? question "FIDO reset is refused"
    A reset is only accepted within 10 seconds of plugging the device in. Unplug, replug and start the reset straight away.

## Codes

??? question "Codes shows *Time not set*"
    The clock is lost at every power-off. Run [`stk-time.py`](reference/stk-time.md).

??? question "Codes says the codes are password-protected"
    The OATH application has an access password, so codes are not shown on screen. Use `ykman oath accounts code` instead, or remove the password.
