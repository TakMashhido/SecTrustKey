#!/usr/bin/env python3
# Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
"""Set the SecTrustKey clock from this computer so the device can show TOTP codes.

The Cardputer ADV has no battery-backed clock: run this after each power-up. It sends the
current UTC time (Unix seconds) over the smart-card interface. Needs pcscd and pyscard
(`pip install pyscard`). Usage: stk-time.py [--reader SUBSTRING]
"""
import argparse
import sys
import time

from smartcard.System import readers

RESCUE_AID = bytes.fromhex("A0583FC19B7E4F21")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--reader", default="SecTrustKey", help="part of the reader name")
    args = ap.parse_args()
    match = [r for r in readers() if args.reader.lower() in str(r).lower()]
    if not match:
        print("SecTrustKey not found. Is it plugged in and is pcscd running?", file=sys.stderr)
        return 2
    conn = match[0].createConnection()
    conn.connect()
    _, sw1, sw2 = conn.transmit([0x00, 0xA4, 0x04, 0x00, len(RESCUE_AID)] + list(RESCUE_AID))
    if (sw1, sw2) != (0x90, 0x00):
        print(f"device refused SELECT ({sw1:02X}{sw2:02X})", file=sys.stderr)
        return 1
    now = int(time.time())
    _, sw1, sw2 = conn.transmit([0x80, 0x1C, 0x02, 0x02, 0x04] + list(now.to_bytes(4, "big")))
    if (sw1, sw2) != (0x90, 0x00):
        print(f"device refused the time ({sw1:02X}{sw2:02X})", file=sys.stderr)
        return 1
    print("clock set to", time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime(now)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
