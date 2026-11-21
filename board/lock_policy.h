// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// Wrong-passphrase policy for the startup lock: never erase anything, slow down guessing.
// The failure count is stored and incremented BEFORE each attempt is checked, so cutting
// power during the check does not give a free try.
#pragma once

#include <cstdint>

namespace cardputer {

inline constexpr std::uint32_t kFreeAttempts = 3;        // no wait for the first three
inline constexpr std::uint32_t kMaxLockoutSeconds = 300;  // the wait never grows past 5 min
inline constexpr std::size_t kPassphraseMin = 12;   // offline guessing of a dumped chip is only slowed by PBKDF2

// Seconds to wait before the next attempt, after `failures` wrong ones in a row:
// 0, 0, 0, then 1, 2, 4, 8 ... capped at five minutes.
constexpr std::uint32_t lockout_seconds(std::uint32_t failures) {
    if (failures < kFreeAttempts) {
        return 0;
    }
    const std::uint32_t shift = failures - kFreeAttempts;
    return shift >= 9 ? kMaxLockoutSeconds
                      : ((1u << shift) < kMaxLockoutSeconds ? (1u << shift) : kMaxLockoutSeconds);
}

}  // namespace cardputer
