---
title: Crash-dump debug build
description: Build SecTrustKey with STK_DEBUG_CRASHDUMP to store crash dumps in flash, show the crashing task and backtrace on the screen, and resolve addresses with addr2line.
---

# Debug build

A debug image stores a crash dump in its own 64 KiB partition and shows the crashing task, fault address and backtrace on the screen at the next boot, for 15 seconds. It also turns a stalled command into a deliberate crash, so the stall can be located.

```bash
scripts/in-container.sh env STK_DEBUG_CRASHDUMP=1 bash scripts/build.sh
```

The output is in `build/cardputer-adv-debug/`. Its partition table adds the crash-dump partition **after** the key store and keeps every other offset, so it can replace a normal build without losing keys: write the partition table at `0x8000` and the application at `0x20000`.

Resolve the addresses shown on the screen:

```bash
scripts/in-container.sh xtensa-esp32s3-elf-addr2line -pfiaC \
  -e build/cardputer-adv-debug/sectrustkey.elf 0x4201b083 0x42017c05
```

!!! warning
    Do not ship the debug image. Going back to a normal build later means writing the normal partition table and application again.
