// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// Cable-presence heuristic for the Cardputer ADV, which has no VBUS sense pin: while the
// host is attached it sends a USB start-of-frame every millisecond, so a long silence means
// the cable was pulled. The SOF timestamp is written on another core and may be newer than
// the caller's clock sample, so the age is signed: a "future" SOF is fresh, not 4e9 ms old.
#pragma once

#include <cstdint>

namespace cardputer {

inline constexpr std::int32_t kSofLostAfterMs = 500;

constexpr bool usb_link_lost(std::uint32_t now_ms, std::uint32_t last_sof_ms) {
    return static_cast<std::int32_t>(now_ms - last_sof_ms) > kSofLostAfterMs;
}

}  // namespace cardputer
