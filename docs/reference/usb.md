---
title: USB interfaces
description: The SecTrustKey USB composite device — HID FIDO (CTAPHID), HID keyboard for OTP, CCID smart card and a vendor interface, and which host software uses each.
---

# USB interfaces

SecTrustKey is a USB 2.0 full-speed composite device (default ID `2E8A:10FE`) with four interfaces:

| Interface | Class | Used by | Carries |
|---|---|---|---|
| FIDO | HID, usage page `0xF1D0` | Browsers, `libfido2`, Windows Hello | CTAPHID: FIDO2 and U2F |
| Keyboard | HID boot keyboard | Any computer | Typed Yubico OTP |
| Smart card | CCID | `pcscd`, Windows smart-card service, GnuPG, `ykman`, OpenSC | ISO 7816 APDUs: OpenPGP, PIV, OATH, management |
| Vendor | Vendor-specific | Web and management tools | CCID-style APDUs |

Commands from every interface run on the same [command thread](../concepts/architecture.md#cores-and-tasks), one at a time.

While the device is in a maintenance screen (passphrase setup, backup, restore), USB is off.
