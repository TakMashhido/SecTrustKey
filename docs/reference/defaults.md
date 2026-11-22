---
title: Factory defaults
description: SecTrustKey factory defaults — OpenPGP and PIV PINs, PUK, FIDO PIN, retry counters and device settings on a freshly flashed device.
---

# Factory defaults

!!! danger "These values are public"
    Change every PIN before you store anything real. The [Health check](../guide/health.md) shows which ones are still at their factory value.

| Application | Secret | Factory value | Minimum length | Tries |
|---|---|---|---|---|
| FIDO2 | PIN | none until you set one | 4 | 8 |
| OpenPGP | User PIN | `123456` | 6 | 3 |
| OpenPGP | Admin PIN | `12345678` | 8 | 3 |
| PIV | PIN | `123456` | 6 | 3 |
| PIV | PUK | `12345678` | 8 | 3 |
| PIV | Management key | the well-known default | — | — |
| Device | Startup passphrase | none | 12 | unlimited, with delays |
| Device | SD backup password | chosen per backup | 12 | — |

| Setting | Default |
|---|---|
| Volume | 30 |
| Brightness | 60 |
| Screen timeout | 1 min |
