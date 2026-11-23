// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// Calendar arithmetic for the Clock settings: typed local time to UTC, the UTC offset label,
// and local hours for the status bar. No time zone database: the user picks an offset.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace cardputer {
namespace clocklogic {

// Offsets in use worldwide run from UTC-12:00 to UTC+14:00, in steps of 15 minutes.
constexpr int kMinOffset = -12 * 60;
constexpr int kMaxOffset = 14 * 60;
constexpr int kOffsetStep = 15;
// Typed times are accepted from 2025 to 2099 (the clock counts as unset before 2025).
constexpr int kFirstYear = 2025;
constexpr int kLastYear = 2099;

inline int clamp_offset(int minutes) {
    if (minutes < kMinOffset) { return kMinOffset; }
    if (minutes > kMaxOffset) { return kMaxOffset; }
    return minutes - (minutes % kOffsetStep);
}

// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's algorithm).
inline std::int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = static_cast<int>(y - era * 400);
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

struct Civil {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

inline Civil civil_from_unix(std::int64_t t) {
    std::int64_t days = t / 86400;
    std::int64_t rem = t % 86400;
    if (rem < 0) { rem += 86400; --days; }
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = static_cast<int>(z - era * 146097);
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    Civil c;
    c.day = doy - (153 * mp + 2) / 5 + 1;
    c.month = mp + (mp < 10 ? 3 : -9);
    c.year = static_cast<int>(yoe + era * 400) + (c.month <= 2 ? 1 : 0);
    c.hour = static_cast<int>(rem / 3600);
    c.minute = static_cast<int>((rem % 3600) / 60);
    c.second = static_cast<int>(rem % 60);
    return c;
}

inline int days_in_month(int y, int m) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : kDays[m - 1];
}

// Reads "YYYY-MM-DD HH:MM" (one or more spaces, or a 'T', between date and time) as local
// time at `offset_minutes` east of UTC, with seconds 0. False for anything else.
inline bool local_to_utc(const char* text, int offset_minutes, std::int64_t* out) {
    if (text == nullptr || out == nullptr) { return false; }
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, used = 0;
    char sep = 0;
    if (std::sscanf(text, "%4d-%2d-%2d%c", &y, &mo, &d, &sep) != 4 || (sep != ' ' && sep != 'T')) {
        return false;
    }
    if (std::sscanf(text, "%*4d-%*2d-%*2d%*[ T]%2d:%2d%n", &h, &mi, &used) != 2) { return false; }
    for (const char* rest = text + used; *rest != '\0'; ++rest) {
        if (*rest != ' ') { return false; }
    }
    if (y < kFirstYear || y > kLastYear || mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) ||
        h < 0 || h > 23 || mi < 0 || mi > 59) {
        return false;
    }
    *out = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 -
           static_cast<std::int64_t>(clamp_offset(offset_minutes)) * 60;
    return true;
}

// "YYYY-MM-DD HH:MM" of `utc` seen at `offset_minutes`.
inline void format_local(std::int64_t utc, int offset_minutes, char* out, std::size_t cap) {
    const Civil c = civil_from_unix(utc + static_cast<std::int64_t>(clamp_offset(offset_minutes)) * 60);
    std::snprintf(out, cap, "%04u-%02u-%02u %02u:%02u", static_cast<unsigned>(c.year) % 10000u,
                  static_cast<unsigned>(c.month) % 100u, static_cast<unsigned>(c.day) % 100u,
                  static_cast<unsigned>(c.hour) % 100u, static_cast<unsigned>(c.minute) % 100u);
}

// "UTC+05:30", "UTC-03:00", "UTC".
inline void format_offset(int minutes, char* out, std::size_t cap) {
    minutes = clamp_offset(minutes);
    if (minutes == 0) {
        std::snprintf(out, cap, "UTC");
        return;
    }
    const int a = minutes < 0 ? -minutes : minutes;
    std::snprintf(out, cap, "UTC%c%02u:%02u", minutes < 0 ? '-' : '+',
                  static_cast<unsigned>(a / 60) % 100u, static_cast<unsigned>(a % 60) % 100u);
}

}  // namespace clocklogic
}  // namespace cardputer
