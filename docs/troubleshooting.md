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

??? question "The device shows a storage error and USB stays off"
    The key store or file store was found damaged, often because a first install was done without `--erase-all` over another firmware's data. Restore an [SD backup](guide/backup.md) from the recovery screen, or flash again with `--erase-all`.

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
