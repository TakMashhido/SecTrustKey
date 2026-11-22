---
title: Security model
description: The SecTrustKey threat model — on-screen confirmation, origin-bound passkeys, application isolation, the startup passphrase and encrypted backups, and the limits of an ESP32-S3 without Secure Boot or flash encryption.
---

# Security model

SecTrustKey runs on a general-purpose microcontroller, not a certified secure element. It is designed to make phishing, silent use and accidental approval hard, and it is honest about what an attacker with the device in hand can do.

## What it protects against

| Threat | How the design helps |
|---|---|
| Phishing and stolen passwords | FIDO2 credentials are bound to the website's origin and never leave the device. |
| Silent use by software on your computer | Every sensitive operation waits for ++enter++ on the device; ++backspace++ refuses. |
| Approving the wrong thing | Prompts name the site and account; long site names keep their registrable end. |
| One application wiping another | Every file belongs to one application, and each reset is limited to its own files. |
| A lost or stolen device that was switched off | With a startup passphrase the master keys are encrypted at rest and asked for before USB starts; wrong tries only add delays. |
| Losing the device | Password-encrypted microSD backup bound to the device. |
| Factory PINs left in place | The Health screen flags them without spending a PIN try. |

## What it does not protect against

- **Reading the flash without a passphrase.** Secure Boot, flash encryption and eFuse key storage are one-way settings and were deliberately not enabled. Without a startup passphrase, anyone who can read the chip's flash can copy every key.
- **A device that is already unlocked.** Once unlocked, ++enter++ proves presence, not identity.
- **A compromised computer capturing PINs.** FIDO, OpenPGP and PIV PINs are typed on the host.
- **Malicious firmware.** The firmware does not verify its own updates.
- **Side-channel and fault attacks** on the chip.

!!! warning "Retry counters and offline guessing"
    PIN retry counters stop guessing on the device. They do not stop someone who has copied the flash and guesses offline, or who rolls the flash back to an earlier state.

## Passphrase strength

The startup passphrase is stretched with PBKDF2-HMAC-SHA256 at a cost calibrated on the chip to about one second per guess. A computer with a copy of the flash guesses far faster. Use at least 12 characters; four or more random words is the practical minimum against a determined attacker.

## Reporting a problem

Report security issues privately to [erkoustavkumar@gmail.com](mailto:erkoustavkumar@gmail.com) rather than in a public issue.
