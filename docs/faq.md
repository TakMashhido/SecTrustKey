---
title: FAQ
description: Frequently asked questions about SecTrustKey — hardware support, comparison with a YubiKey, security, backups, operating systems and licensing.
---

# FAQ

??? question "Does it work on the original Cardputer?"
    No. SecTrustKey targets the **Cardputer ADV**, whose keyboard controller (TCA8418) differs from the original Cardputer's.

??? question "Is it as secure as a YubiKey?"
    No. A YubiKey keeps keys in a certified secure element that resists physical extraction. SecTrustKey runs on an ESP32-S3 without Secure Boot or flash encryption, so physical access to the chip matters. In exchange you get a screen that shows exactly what you are approving, open firmware you can audit and build, and encrypted backups. Read the [security model](concepts/security.md).

??? question "Which operating systems work?"
    FIDO2 and passkeys work in browsers on Linux, macOS and Windows with no drivers. OpenPGP, PIV and OATH need the computer's smart-card service and have been tested on Linux.

??? question "Can I move my keys to a new Cardputer?"
    Not with an SD backup: a backup can only be restored on the device that made it. Keep a second authenticator registered instead.

??? question "What happens if I forget the startup passphrase?"
    The keys cannot be recovered from the device. Restore an SD backup made **before** the passphrase was set, or erase and set the device up again. Wrong guesses never wipe anything, so there is no harm in trying.

??? question "Does the device need a battery?"
    No, it runs from USB. The battery lets you read TOTP codes away from a computer, but the clock must be set after each power-up.

??? question "Can I change the USB name and ID?"
    Yes, at build time. See [USB identity](customize/usb-identity.md).

??? question "Under which licence is it released?"
    The GNU AGPL v3.0, like the projects it builds on. See [License & credits](license.md).
