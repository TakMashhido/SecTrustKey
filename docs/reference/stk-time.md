---
title: Clock tool
description: stk-time.py sets the SecTrustKey clock over PC/SC so TOTP codes can be shown on the device; the APDU it sends and how to use it.
---

# Clock tool

[Download `stk-time.py`](../assets/downloads/stk-time.py){ .md-button }

The Cardputer has no battery-backed clock. Since v1.2.0 the device sets it itself under **Settings → Clock** (Wi-Fi or typed; see [TOTP codes](../guide/codes.md#set-the-clock)). `stk-time.py` remains an alternative that sets it from your computer over PC/SC.

```bash
pip install pyscard          # needs pcscd on Linux
python3 stk-time.py
# clock set to 2026-11-22 10:20:00 UTC
```

## What it sends

1. `SELECT` of the rescue application, AID `A0 58 3F C1 9B 7E 4F 21`.
2. One command with the current Unix time:

| Field | Value |
|---|---|
| CLA | `0x80` |
| INS | `0x1C` |
| P1, P2 | `0x02`, `0x02` |
| Data | 4 bytes, big-endian seconds since 1970-01-01 UTC |

The clock runs until the device is switched off; set it again after each power-up.
