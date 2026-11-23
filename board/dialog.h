// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// Builders for the full-screen dialogs the board raises outside the normal menu: backup
// notices, confirmations, progress, and masked secret entry. A dialog has a mood, which sets
// its accent colour and icon so success, caution and danger look different at a glance.
#pragma once

#include <cstdio>
#include <cstring>

#include "about.h"
#include "model.h"
#include "password_meter.h"

namespace cardputer {

enum class Mood { Info, Good, Warn, Bad };

inline Accent mood_accent(Mood mood) {
    switch (mood) {
        case Mood::Good: return Accent::Green;
        case Mood::Warn: return Accent::Amber;
        case Mood::Bad:  return Accent::Red;
        case Mood::Info:
        default:         return Accent::Blue;
    }
}

inline Icon mood_icon(Mood mood) {
    switch (mood) {
        case Mood::Good: return Icon::Check;
        case Mood::Warn:
        case Mood::Bad:  return Icon::Warn;
        case Mood::Info:
        default:         return Icon::Info;
    }
}

// A single-message dialog. `footer` is the key legend, e.g. "ENT:OK" or "ENT:YES DEL:NO".
inline Snapshot make_dialog(const char* header, const char* title, const char* line,
                            const char* footer, Mood mood) {
    Snapshot view;
    view.layout = Layout::Prompt;
    view.header = header;
    view.title = title;
    view.icon = mood_icon(mood);
    view.accent = mood_accent(mood);
    if (line != nullptr && line[0] != '\0') {
        std::snprintf(view.rows[0].text, sizeof(view.rows[0].text), "%s", line);
        view.row_count = 1;
    }
    view.footer = footer;
    return view;
}

// Masked entry of a secret of `length` characters (`typed` points at them for the strength
// estimate). The field reads "Too short" until `minimum` characters are typed.
// A long operation: what it is, what it is doing now, which step of how many, and how far.
// `percent` < 0 shows an indeterminate bar. `phase` (0..99) animates the sheen.
inline Snapshot make_progress(const char* title, const char* detail, int step, int steps,
                              int percent, Accent accent, int phase,
                              const char* footer = "Keep the device powered",
                              const char* context = "SD card") {
    Snapshot view;
    view.layout = Layout::Progress;
    view.header = context;   // the status bar names the area; the big title names the task
    view.title = title;
    view.accent = accent;
    view.progress = percent > 100 ? 100 : percent;
    view.phase = ((phase % 100) + 100) % 100;
    std::snprintf(view.rows[0].text, sizeof(view.rows[0].text), "%s", detail ? detail : "");
    if (steps > 1) {
        std::snprintf(view.rows[0].value, sizeof(view.rows[0].value), "Step %d of %d", step, steps);
    }
    view.row_count = 1;
    view.footer = footer;
    return view;
}

// Boot reveal: `percent` (0..100) draws the hairline under the name.
inline Snapshot make_splash(int percent) {
    Snapshot view;
    view.layout = Layout::Splash;
    view.title = "SecTrustKey";
    view.subtitle = "Hardware security key";
    view.icon = Icon::Shield;
    view.accent = Accent::Teal;
    view.progress = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    view.footer = kCredit;
    return view;
}

// Visible entry of plain text such as a network name or a date. No strength meter.
inline Snapshot make_text_entry(const char* title, const char* text, std::size_t maximum,
                                const char* hint, const char* footer, Icon icon, Accent accent) {
    Snapshot view;
    view.layout = Layout::Entry;
    view.title = title;
    view.icon = icon;
    view.accent = accent;
    view.footer = footer;
    view.subtitle = hint;
    view.meter = -1;
    std::snprintf(view.rows[0].text, sizeof(view.rows[0].text), "%s", text ? text : "");
    view.rows[0].mono = true;
    std::snprintf(view.rows[0].value, sizeof(view.rows[0].value), "%u / %u",
                  static_cast<unsigned>(std::strlen(view.rows[0].text)), static_cast<unsigned>(maximum));
    view.row_count = 1;
    return view;
}

inline Snapshot make_entry(const char* title, const char* typed, std::size_t length,
                           std::size_t minimum, std::size_t maximum, const char* hint,
                           const char* footer) {
    Snapshot view;
    view.layout = Layout::Entry;
    view.title = title;
    view.icon = Icon::Lock;
    view.footer = footer;
    view.subtitle = hint;
    const PasswordStrength strength = measure_password(typed, length);
    static const char* const kTierLabel[4] = {"Weak", "Fair", "Good", "Strong"};
    view.value[0] = '\0';
    // Below the minimum the strength is irrelevant: say so instead of "Fair".
    const bool too_short = length < minimum;
    std::snprintf(view.value, sizeof(view.value), "%s",
                  length == 0 ? "" : (too_short ? "Too short" : kTierLabel[strength.tier]));
    view.meter = length == 0 ? 0 : (strength.meter < 6 ? 6 : strength.meter);
    view.accent = too_short || strength.tier <= 0
                      ? Accent::Red
                      : (strength.tier == 1 ? Accent::Amber : Accent::Green);
    const std::size_t shown = length < 16 ? length : 16;
    std::memset(view.rows[0].text, '*', shown);
    view.rows[0].text[shown] = '\0';
    std::snprintf(view.rows[0].value, sizeof(view.rows[0].value), "%u / %u",
                  static_cast<unsigned>(length), static_cast<unsigned>(maximum));
    view.row_count = 1;
    return view;
}

}  // namespace cardputer
