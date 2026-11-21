// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// A rough strength estimate for a passphrase being typed on the device. It is guidance, not
// a guarantee: it assumes characters are chosen independently, so a famous phrase scores
// higher than it deserves. What it does enforce is the thing that matters most against an
// offline attack on a dumped flash image: LENGTH. It cannot see real-world guessability.
#pragma once

#include <cmath>
#include <cstddef>

namespace cardputer {

struct PasswordStrength {
    double bits = 0;      // estimated entropy
    int meter = 0;        // 0..100 for a progress bar
    const char* label = "Too short";
    int tier = 0;         // 0 weak .. 3 strong, for colour
};

inline PasswordStrength measure_password(const char* s, std::size_t n) {
    PasswordStrength out;
    if (s == nullptr || n == 0) {
        return out;
    }
    bool lower = false, upper = false, digit = false, other = false;
    unsigned char seen[256] = {};
    std::size_t distinct = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 'a' && c <= 'z') lower = true;
        else if (c >= 'A' && c <= 'Z') upper = true;
        else if (c >= '0' && c <= '9') digit = true;
        else other = true;
        if (!seen[c]) {
            seen[c] = 1;
            ++distinct;
        }
    }
    double pool = 0;
    if (lower) pool += 26;
    if (upper) pool += 26;
    if (digit) pool += 10;
    if (other) pool += 33;
    // Repeated characters add little: count at most twice as many "fresh" characters as
    // there are distinct ones.
    const std::size_t effective = n < distinct * 2 ? n : distinct * 2;
    out.bits = static_cast<double>(effective) * std::log2(pool);
    const double scaled = out.bits * 100.0 / 100.0;
    out.meter = scaled > 100.0 ? 100 : static_cast<int>(scaled);
    if (out.bits < 36) { out.label = "Weak"; out.tier = 0; }
    else if (out.bits < 60) { out.label = "Fair"; out.tier = 1; }
    else if (out.bits < 80) { out.label = "Good"; out.tier = 2; }
    else { out.label = "Strong"; out.tier = 3; }
    return out;
}

}  // namespace cardputer
