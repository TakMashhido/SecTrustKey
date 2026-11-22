---
title: PIV smart card
description: Use SecTrustKey as a PIV smart card with Yubico ykman or OpenSC — change PIN, PUK and management key, generate keys and certificates.
---

# PIV smart card

SecTrustKey includes a PIV application, managed with Yubico's `ykman` or OpenSC tools. On Linux, set up `pcscd` first as described in [OpenPGP and GnuPG](openpgp.md#linux-let-pcscd-see-the-reader).

## Change the factory secrets

```bash
ykman --reader SecTrustKey piv access change-pin                  # factory 123456
ykman --reader SecTrustKey piv access change-puk                  # factory 12345678
ykman --reader SecTrustKey piv access change-management-key --generate --protect
```

## Keys and certificates

```bash
ykman --reader SecTrustKey piv keys generate --algorithm ECCP256 9a pub.pem
ykman --reader SecTrustKey piv certificates generate --subject "CN=Koustav" 9a pub.pem
ykman --reader SecTrustKey piv info
```

| Slot | Usual purpose |
|---|---|
| `9a` | Authentication (login, SSH via PKCS#11) |
| `9c` | Digital signature |
| `9d` | Key management (decryption) |
| `9e` | Card authentication |

!!! note
    PIV operations do not show a confirmation prompt; they are protected by the PIV PIN.
