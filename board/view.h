// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// Screen rendering for the 240x135 panel. Pure functions of a Snapshot: no state, no device
// access. They are templates over the drawing surface, so the same code runs on the real
// display, in the off-screen frame buffer, and in the host tests and preview tool with the
// real fonts. Include the graphics library before this header.
//
// Typography is deliberate: bitmap fonts are only crisp at their native size, so nothing is
// scaled. Titles use FreeSansBold12, primary text FreeSans/FreeSansBold 9, and everything
// secondary (hints, key legends, status) uses the compact pixel font Font2.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "about.h"
#include "model.h"
#include "ui_icons.h"
#include "ui_theme.h"

namespace cardputer::lcd {

// ---------------------------------------------------------------- typography
enum class Font { Small, Body, Bold, Title, Mono, Code };

template <class D>
void use_font(D& d, Font f) {
    switch (f) {
        case Font::Small: d.setFont(&lgfx::fonts::Font2); break;
        case Font::Bold:  d.setFont(&lgfx::fonts::FreeSansBold9pt7b); break;
        case Font::Title: d.setFont(&lgfx::fonts::FreeSansBold12pt7b); break;
        case Font::Mono:  d.setFont(&lgfx::fonts::FreeMonoBold9pt7b); break;
        case Font::Code:  d.setFont(&lgfx::fonts::FreeMonoBold24pt7b); break;
        case Font::Body:
        default:          d.setFont(&lgfx::fonts::FreeSans9pt7b); break;
    }
    d.setTextSize(1.0f);
}

// Fonts here carry ASCII only. An unsupported UTF-8 scalar becomes exactly one '?', never
// several unrelated glyphs, and a scalar is never split.
inline const char* next_glyph(const char* text, char* glyph) {
    const auto first = static_cast<unsigned char>(*text++);
    *glyph = first >= 32 && first < 127 ? static_cast<char>(first) : '?';
    if (first >= 128) {
        while ((*text & 0xC0) == 0x80) {
            ++text;
        }
    }
    return text;
}

// Copies `input` into `out` with non-ASCII scalars replaced, at most cap-1 glyphs.
inline void sanitize(const char* input, char* out, std::size_t cap) {
    std::size_t n = 0;
    while (input != nullptr && *input != '\0' && n + 1 < cap) {
        input = next_glyph(input, &out[n++]);
    }
    out[n] = '\0';
}

template <class D>
int text_width(D& d, Font f, const char* s) {
    use_font(d, f);
    return d.textWidth(s);
}

// Shortens `buffer` in place until it fits `max_w`, replacing the removed part with "...".
template <class D>
void clip_to_width(D& d, char* buffer, std::size_t cap, int max_w, Clip clip) {
    if (d.textWidth(buffer) <= max_w) {
        return;
    }
    const std::size_t len = std::strlen(buffer);
    char out[304];
    for (std::size_t drop = 1; drop + 3 < len; ++drop) {
        switch (clip) {
            case Clip::Head:
                std::snprintf(out, sizeof(out), "...%s", buffer + drop + 2);
                break;
            case Clip::Middle: {
                const std::size_t keep = len - drop - 3;       // characters kept besides "..."
                const std::size_t front = (keep + 1) / 2;
                std::snprintf(out, sizeof(out), "%.*s...%s", static_cast<int>(front), buffer,
                              buffer + len - (keep - front));
                break;
            }
            case Clip::Tail:
            default:
                std::snprintf(out, sizeof(out), "%.*s...", static_cast<int>(len - drop - 3), buffer);
                break;
        }
        if (d.textWidth(out) <= max_w) {
            std::snprintf(buffer, cap, "%s", out);
            return;
        }
    }
    std::snprintf(buffer, cap, "...");
}

// Draws one line with its top at `y` in `color`, shortened per `clip` so it fits `max_w` px.
// Returns the drawn width.
template <class D>
int draw_text(D& d, Font f, const char* input, int x, int y, std::uint16_t color,
              int max_w = 1000, Clip clip = Clip::Tail) {
    char buffer[300];
    sanitize(input, buffer, sizeof(buffer));
    use_font(d, f);
    clip_to_width(d, buffer, sizeof(buffer), max_w, clip);
    d.setTextColor(color);
    d.drawString(buffer, x, y);
    return d.textWidth(buffer);
}

// The y that centres a line of font `f` inside a box starting at `box_y`, `box_h` tall.
template <class D>
int centre_y(D& d, Font f, int box_y, int box_h) {
    use_font(d, f);
    return box_y + (box_h - d.fontHeight()) / 2;
}

template <class D>
void draw_text_center(D& d, Font f, const char* s, int cx, int y, std::uint16_t color,
                      int max_w = 1000) {
    char buffer[300];
    sanitize(s, buffer, sizeof(buffer));
    const int w = std::min(text_width(d, f, buffer), max_w);
    draw_text(d, f, buffer, cx - w / 2, y, color, max_w);
}

template <class D>
void draw_text_right(D& d, Font f, const char* s, int right, int y, std::uint16_t color,
                     int max_w = 1000) {
    char buffer[300];
    sanitize(s, buffer, sizeof(buffer));
    const int w = std::min(text_width(d, f, buffer), max_w);
    draw_text(d, f, buffer, right - w, y, color, max_w);
}

// The largest of Title / Bold in which `s` fits `max_w`.
template <class D>
Font fit_font(D& d, const char* s, int max_w) {
    char buffer[300];
    sanitize(s, buffer, sizeof(buffer));
    return text_width(d, Font::Title, buffer) <= max_w ? Font::Title : Font::Bold;
}

// ------------------------------------------------------------- key legends
// "ENT" -> "Enter", "DEL" -> "Del": the printed legend of a Cardputer key.
inline const char* key_name(const char* token) {
    if (std::strcmp(token, "ENT") == 0) return "Enter";
    if (std::strcmp(token, "DEL") == 0) return "Del";
    if (std::strcmp(token, "FN+DEL") == 0) return "Fn+Del";
    return token;
}

// "YES" -> "Approve": a confirmation says what pressing the key does.
inline void action_label(const char* token, char* out, std::size_t cap) {
    if (std::strcmp(token, "YES") == 0) { std::snprintf(out, cap, "Approve"); return; }
    if (std::strcmp(token, "NO") == 0)  { std::snprintf(out, cap, "Refuse"); return; }
    if (std::strcmp(token, "OK") == 0)  { std::snprintf(out, cap, "OK"); return; }
    if (std::strcmp(token, "DEL") == 0) { std::snprintf(out, cap, "Delete"); return; }
    std::size_t n = 0;
    for (; token[n] != '\0' && n + 1 < cap; ++n) {
        const char c = token[n];
        out[n] = n == 0 ? c : static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    out[n] = '\0';
}

struct Chip {
    char key[12];
    char label[48];   // room for a plain footer message such as the home credit
};

// Splits "ENT:OK DEL:BACK" into chips; a token without ':' becomes a label-only chip.
inline int parse_footer(const char* footer, Chip* chips, int max_chips) {
    int count = 0;
    const char* p = footer == nullptr ? "" : footer;
    // A plain message such as "PLEASE WAIT" is one label, not two chips.
    if (*p != '\0' && std::strchr(p, ':') == nullptr) {
        Chip chip = {};
        // An all-caps message ("PLEASE WAIT") becomes sentence case; anything already
        // written in mixed case (a name, a credit) keeps its capitals.
        bool has_lower = false;
        for (const char* q = p; *q; ++q) {
            has_lower = has_lower || (*q >= 'a' && *q <= 'z');
        }
        std::size_t n = 0;
        for (; p[n] != '\0' && n + 1 < sizeof(chip.label); ++n) {
            const char c = p[n];
            chip.label[n] = n == 0 || has_lower ? c : static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        chips[0] = chip;
        return max_chips > 0 ? 1 : 0;
    }
    while (*p != '\0' && count < max_chips) {
        while (*p == ' ') ++p;
        if (*p == '\0') break;
        char token[32] = {};
        int n = 0;
        while (*p != '\0' && *p != ' ' && n < 31) token[n++] = *p++;
        Chip chip = {};
        char* colon = std::strchr(token, ':');
        if (colon != nullptr) {
            *colon = '\0';
            std::snprintf(chip.key, sizeof(chip.key), "%s", key_name(token));
            action_label(colon + 1, chip.label, sizeof(chip.label));
        } else {
            action_label(token, chip.label, sizeof(chip.label));
        }
        chips[count++] = chip;
    }
    return count;
}

// A key cap: the Enter and Delete keys are drawn as their symbols, which are shorter and
// read faster than the words. Returns the width used.
template <class D>
int draw_keycap(D& d, const char* key, int x, int y, int h, std::uint16_t cap_bg,
                std::uint16_t fg) {
    const bool fn = std::strncmp(key, "Fn+", 3) == 0;
    const char* name = fn ? key + 3 : key;
    const bool enter = std::strcmp(name, "Enter") == 0;
    const bool back = std::strcmp(name, "Del") == 0;
    const int icon = h - 4;
    int w = (enter || back) ? icon + 8 : text_width(d, Font::Small, name) + 8;
    if (fn) {
        w += text_width(d, Font::Small, "Fn") + 3;
    }
    d.fillRoundRect(x, y, w, h, 3, cap_bg);
    int cursor = x + 4;
    if (fn) {
        cursor += draw_text(d, Font::Small, "Fn", cursor, centre_y(d, Font::Small, y, h), fg) + 3;
    }
    if (enter || back) {
        draw_icon(d, enter ? Glyph::Enter : Glyph::Backspace, cursor, y + 2, icon, fg, cap_bg);
    } else {
        draw_text(d, Font::Small, name, cursor, centre_y(d, Font::Small, y, h), fg);
    }
    return w;
}

// ------------------------------------------------------------- small widgets
template <class D>
void draw_battery(D& d, int x, int y, int percent, bool charging) {
    const std::uint16_t color = charging ? kGood
                                         : (percent <= 15 ? kBad : (percent <= 30 ? kWarn : kMuted));
    d.drawRoundRect(x, y, 19, 10, 2, color);
    d.fillRect(x + 19, y + 3, 2, 4, color);
    const int fill = std::clamp(percent < 0 ? 0 : percent, 0, 100) * 15 / 100;
    if (fill > 0) {
        d.fillRect(x + 2, y + 2, fill, 6, color);
    }
}

template <class D>
void draw_status_bar(D& d, const Snapshot& snap, std::uint16_t accent) {
    d.fillRect(0, 0, kWidth, kBarHeight, kBar);
    d.drawFastHLine(0, kBarHeight, kWidth, kLine);

    // A transient message owns the whole bar while it is shown, so it is never clipped.
    if (snap.status[0] != '\0') {
        draw_icon(d, Glyph::Warn, kMargin, 3, 12, kWarn, kBar);
        draw_text(d, Font::Bold, snap.status, kMargin + 18, 1, kWarn, kWidth - 2 * kMargin - 18);
        return;
    }

    // The right-hand cluster is laid out first; the title gets whatever width is left.
    const StatusBar& bar = snap.bar;
    const int ty = centre_y(d, Font::Small, 0, kBarHeight);
    int right = kWidth - kMargin;
    if (bar.battery >= 0 || bar.charging) {
        right -= 21;
        draw_battery(d, right, 4, bar.battery, bar.charging);
        right -= 8;
    }
    if (bar.time_known) {
        char clock[8];
        std::snprintf(clock, sizeof(clock), "%02u:%02u", bar.hour, bar.minute);
        const int w = text_width(d, Font::Small, clock);
        draw_text(d, Font::Small, clock, right - w, ty, kText);
        right -= w + 8;
    }
    if (bar.locked) {
        right -= 11;
        draw_icon(d, Glyph::Lock, right, 3, 12, kGood, kBar);
        right -= 8;
    }
    if (bar.sd) {
        right -= 12;
        draw_icon(d, Glyph::Sd, right, 3, 12, kMuted, kBar);
        right -= 8;
    }
    const int usb_w = text_width(d, Font::Small, "USB");
    right -= usb_w;
    draw_text(d, Font::Small, "USB", right, ty, bar.usb ? accent : kMuted);
    const int title_w = right - kMargin - 8;

    draw_text(d, Font::Bold, snap.title, kMargin, 1, kText, title_w);
}

// Key-cap hints along the bottom edge.
template <class D>
void draw_footer(D& d, const char* footer) {
    const int top = kHeight - kFooterHeight;
    d.fillRect(0, top, kWidth, kFooterHeight, kBar);
    d.drawFastHLine(0, top, kWidth, kLine);
    Chip chips[4];
    const int count = parse_footer(footer, chips, 4);
    const int ty = centre_y(d, Font::Small, top, kFooterHeight);
    int x = kMargin;
    for (int i = 0; i < count; ++i) {
        if (chips[i].key[0] != '\0') {
            x += draw_keycap(d, chips[i].key, x, top + 3, kFooterHeight - 6, kSurfaceHi, kText) + 4;
        }
        x += draw_text(d, Font::Small, chips[i].label, x, ty, kMuted) + 14;
    }
}

// A rounded surface with a glyph, used for list icons.
template <class D>
void draw_chip(D& d, Glyph g, int x, int y, int box, int icon_size, std::uint16_t tint,
               std::uint16_t color) {
    d.fillRoundRect(x, y, box, box, 5, tint);
    draw_icon(d, g, x + (box - icon_size) / 2, y + (box - icon_size) / 2, icon_size, color, tint);
}

// ----------------------------------------------------------------- screens
template <class D>
void draw_home(D& d, const Snapshot& snap) {
    constexpr int kCols = 4;
    constexpr int kTileW = 52;
    constexpr int kTileH = 30;
    constexpr int kGap = 6;
    const int left = (kWidth - (kCols * kTileW + (kCols - 1) * kGap)) / 2;
    for (int i = 0; i < snap.tile_count && i < 8; ++i) {
        const Tile& tile = snap.tiles[i];
        const int x = left + (i % kCols) * (kTileW + kGap);
        const int y = kContentTop + 4 + (i / kCols) * (kTileH + kGap);
        const bool selected = i == snap.tile_selected;
        const std::uint16_t color = accent_color(tile.accent);
        const std::uint16_t fill = selected ? accent_tint(tile.accent) : kSurface;
        d.fillRoundRect(x, y, kTileW, kTileH, 7, fill);
        if (selected) {
            d.drawRoundRect(x, y, kTileW, kTileH, 7, color);
            d.drawRoundRect(x + 1, y + 1, kTileW - 2, kTileH - 2, 6, color);
        }
        draw_icon(d, tile.glyph, x + (kTileW - 20) / 2, y + (kTileH - 20) / 2, 20,
                  tile.enabled ? color : kLine, fill);
    }
    if (snap.tile_selected >= 0 && snap.tile_selected < snap.tile_count) {
        const Tile& tile = snap.tiles[snap.tile_selected];
        const int top = kContentTop + 4 + 2 * (kTileH + kGap);
        const int h = kContentBottom - top;
        const int w = draw_text(d, Font::Title, tile.label, kMargin + 2,
                                centre_y(d, Font::Title, top, h), kText, 150);
        draw_text(d, Font::Small, tile.hint, kMargin + 2 + w + 8,
                  centre_y(d, Font::Small, top, h) + 2, kMuted,
                  kWidth - (kMargin + 2 + w + 8) - kMargin);
    }
}

template <class D>
void draw_list(D& d, const Snapshot& snap) {
    // Four rows fill the screen; with three or fewer the selected row's detail line fits.
    const bool dense = snap.row_count > 3;
    const int row_h = dense ? 22 : 24;
    const int gap = dense ? 1 : 2;
    int selected_row = -1;
    for (int i = 0; i < snap.row_count && i < 4; ++i) {
        const Line& row = snap.rows[i];
        const int y = kContentTop + 2 + i * (row_h + gap);
        const std::uint16_t color = accent_color(row.accent);
        d.fillRoundRect(kMargin, y, kWidth - 2 * kMargin, row_h, 6,
                        row.selected ? kSurfaceHi : kSurface);
        if (row.selected) {
            d.fillRoundRect(kMargin, y + 3, 3, row_h - 6, 1, color);
            selected_row = i;
        }
        draw_chip(d, row.icon, kMargin + 6, y + 2, row_h - 4, 14, accent_tint(row.accent), color);
        const Font f = row.selected ? Font::Bold : Font::Body;
        const Font vf = row.mono ? Font::Mono : Font::Bold;
        const int value_w = row.value[0] != '\0' ? text_width(d, vf, row.value) + 8 : 0;
        draw_text(d, f, row.text, kMargin + 6 + (row_h - 4) + 8, centre_y(d, f, y, row_h), kText,
                  kWidth - 2 * kMargin - (row_h - 4) - 22 - value_w, row.clip);
        if (value_w > 0) {
            draw_text_right(d, vf, row.value, kWidth - kMargin - 8, centre_y(d, vf, y, row_h),
                            row.mono ? kText : color);
        }
    }
    if (snap.progress >= 0) {
        // Time left in the current step of a one-time code: a hairline under the status bar.
        const int full = kWidth - 2 * kMargin;
        d.fillRect(kMargin, kContentTop - 1, full, 2, kSurface);
        d.fillRect(kMargin, kContentTop - 1, std::clamp(snap.progress, 0, 100) * full / 100, 2,
                   accent_color(snap.accent));
    }
    if (!dense && selected_row >= 0 && snap.rows[selected_row].sub[0] != '\0') {
        const int top = kContentBottom - 17;
        draw_text(d, Font::Small, snap.rows[selected_row].sub, kMargin + 4,
                  centre_y(d, Font::Small, top, 16), kMuted, kWidth - 2 * kMargin - 8,
                  Clip::Middle);
    }
}

template <class D>
void draw_details(D& d, const Snapshot& snap) {
    const std::uint16_t color = accent_color(snap.accent);
    constexpr int kTextX = kMargin + 46;
    const int text_w = kWidth - kTextX - kMargin;
    draw_chip(d, snap.icon, kMargin, kContentTop + 8, 36, 22, accent_tint(snap.accent), color);
    if (snap.row_count > 0) {
        draw_text(d, fit_font(d, snap.rows[0].text, text_w), snap.rows[0].text, kTextX,
                  kContentTop + 5, kText, text_w, snap.rows[0].clip);
    }
    if (snap.row_count > 1) {
        draw_text(d, Font::Body, snap.rows[1].text, kTextX, kContentTop + 32, kMuted, text_w,
                  snap.rows[1].clip);
    }
    if (snap.subtitle[0] != '\0') {
        // An instruction (a command or a menu path) must be readable in full: one line when
        // it fits, otherwise two lines broken at the space closest to the middle.
        const int inner = kWidth - 2 * kMargin - 12;
        if (text_width(d, Font::Small, snap.subtitle) <= inner) {
            const int top = kContentBottom - 24;
            d.fillRoundRect(kMargin, top, kWidth - 2 * kMargin, 22, 6, kSurface);
            draw_text_center(d, Font::Small, snap.subtitle, kWidth / 2,
                             centre_y(d, Font::Small, top, 22), kMuted, inner);
        } else {
            char a[80] = {};
            char b[80] = {};
            const std::size_t len = std::strlen(snap.subtitle);
            std::size_t split = len;
            for (std::size_t i = 0, best = len; i < len; ++i) {
                if (snap.subtitle[i] == ' ') {
                    const std::size_t dist = i > len / 2 ? i - len / 2 : len / 2 - i;
                    if (dist < best) { best = dist; split = i; }
                }
            }
            std::snprintf(a, sizeof(a), "%.*s", static_cast<int>(split), snap.subtitle);
            std::snprintf(b, sizeof(b), "%s", split < len ? snap.subtitle + split + 1 : "");
            const int top = kContentBottom - 38;
            d.fillRoundRect(kMargin, top, kWidth - 2 * kMargin, 36, 6, kSurface);
            draw_text_center(d, Font::Small, a, kWidth / 2, top + 2, kMuted, inner);
            draw_text_center(d, Font::Small, b, kWidth / 2, top + 18, kMuted, inner);
        }
    }
}

template <class D>
void draw_editor(D& d, const Snapshot& snap) {
    const std::uint16_t color = accent_color(snap.accent);
    draw_chip(d, snap.icon, kWidth / 2 - 12, kContentTop + 2, 24, 16, accent_tint(snap.accent), color);
    draw_text_center(d, Font::Small, snap.rows[0].text, kWidth / 2, kContentTop + 26, kMuted);
    draw_text_center(d, Font::Code, snap.value, kWidth / 2, kContentTop + 43, kText, kWidth - 80);
    if (snap.meter >= 0) {
        const int x = 40, w = kWidth - 80, y = kContentBottom - 8;
        d.fillRoundRect(x, y, w, 6, 3, kSurfaceHi);
        const int fill = std::clamp(snap.meter, 0, 100) * w / 100;
        if (fill > 0) d.fillRoundRect(x, y, std::max(fill, 6), 6, 3, color);
    }
    draw_icon(d, Glyph::Left, 10, kContentTop + 48, 20, kMuted, kBg);
    draw_icon(d, Glyph::Right, kWidth - 30, kContentTop + 48, 20, kMuted, kBg);
}

// Masked entry of a secret. The field shows one dot per character typed (capped at 16, the
// counter carries the true length), a strength bar, and a hint. The accent colours the bar:
// red for weak, amber for fair, green for good and strong.
template <class D>
void draw_entry(D& d, const Snapshot& snap) {
    const std::uint16_t color = accent_color(snap.accent);
    constexpr int kFieldTop = kContentTop + 3;
    constexpr int kFieldH = 34;
    d.fillRoundRect(kMargin, kFieldTop, kWidth - 2 * kMargin, kFieldH, 8, kSurface);
    d.drawRoundRect(kMargin, kFieldTop, kWidth - 2 * kMargin, kFieldH, 8, color);
    draw_chip(d, snap.icon, kMargin + 5, kFieldTop + 5, 24, 16, accent_tint(snap.accent), color);
    const int dots = static_cast<int>(std::strlen(snap.rows[0].text));
    if (snap.rows[0].mono) {
        // Plain entry (network name, time): show the end of the text and a caret after it.
        constexpr int kTextX = kMargin + 42;
        constexpr int kTextW = kWidth - 2 * kMargin - 42 - 10;
        const char* shown = snap.rows[0].text;
        while (*shown != '\0' && text_width(d, Font::Body, shown) > kTextW) {
            ++shown;
        }
        const int ty = centre_y(d, Font::Body, kFieldTop, kFieldH);
        draw_text(d, Font::Body, shown, kTextX, ty, kText, kTextW);
        const int caret = kTextX + (*shown != '\0' ? text_width(d, Font::Body, shown) + 1 : 0);
        d.fillRect(caret, kFieldTop + 9, 2, kFieldH - 18, color);
    } else {
        for (int i = 0; i < dots && i < 16; ++i) {
            d.fillCircle(kMargin + 42 + i * 11, kFieldTop + kFieldH / 2, 4, kText);
        }
    }
    if (dots == 0 && !snap.rows[0].mono) {
        draw_text(d, Font::Small, "type to begin", kMargin + 42, centre_y(d, Font::Small, kFieldTop, kFieldH),
                  kMuted, 150);
    }
    const int bar_top = kFieldTop + kFieldH + 8;
    if (snap.meter >= 0) {   // the strength meter belongs to secrets only
        d.fillRoundRect(kMargin, bar_top, kWidth - 2 * kMargin, 6, 3, kSurfaceHi);
        const int fill = std::clamp(snap.meter, 0, 100) * (kWidth - 2 * kMargin) / 100;
        if (fill > 0) {
            d.fillRoundRect(kMargin, bar_top, std::max(fill, 6), 6, 3, color);
        }
    }
    const int row_top = bar_top + 9;
    draw_text(d, Font::Small, snap.value, kMargin, centre_y(d, Font::Small, row_top, 16), color,
              100);
    draw_text_right(d, Font::Small, snap.rows[0].value, kWidth - kMargin,
                    centre_y(d, Font::Small, row_top, 16), kMuted, 100);
    if (snap.subtitle[0] != '\0') {
        draw_text(d, Font::Small, snap.subtitle, kMargin, centre_y(d, Font::Small, row_top + 17, 16),
                  kMuted, kWidth - 2 * kMargin);
    }
}

// Boot reveal, the one orchestrated motion: the shield, the name, and a hairline drawing
// itself under the name as `progress` goes from 0 to 100.
template <class D>
void draw_splash(D& d, const Snapshot& snap) {
    const std::uint16_t color = accent_color(snap.accent);
    draw_icon(d, snap.icon, kWidth / 2 - 18, 14, 36, color, kBg);
    draw_text_center(d, Font::Title, snap.title, kWidth / 2, 56, kText);
    const int line_w = 120;
    const int x = kWidth / 2 - line_w / 2;
    d.fillRect(x, 82, line_w, 2, kSurface);
    const int drawn = std::clamp(snap.progress, 0, 100) * line_w / 100;
    if (drawn > 0) {
        d.fillRect(x, 82, drawn, 2, color);
    }
    draw_text_center(d, Font::Small, snap.subtitle, kWidth / 2, 90, kMuted);
}

// About: the QR code opens the developer's site. Beside it the name (two lines, so it is
// never clipped), website and repository; along the bottom the e-mail and the build.
template <class D>
void draw_about(D& d, const Snapshot& snap) {
    constexpr int kQr = 60;
    const int qr_x = kMargin + 3;
    const int qr_y = kContentTop + 6;
    d.fillRoundRect(qr_x - 3, qr_y - 3, kQr + 6, kQr + 6, 6, kText);   // quiet zone
    d.qrcode(kAboutQr, qr_x, qr_y, kQr, 2);
    const int x = qr_x + kQr + 10;
    const int w = kWidth - x - kMargin;
    // Split "First Middle Last" before the last word.
    char first[40] = {};
    const char* last = std::strrchr(kAboutName, ' ');
    const std::size_t n = last ? static_cast<std::size_t>(last - kAboutName) : std::strlen(kAboutName);
    std::memcpy(first, kAboutName, n < sizeof(first) - 1 ? n : sizeof(first) - 1);
    int y = kContentTop + 4;
    draw_text(d, Font::Bold, first, x, y, kText, w);
    y += 18;
    if (last) {
        draw_text(d, Font::Bold, last + 1, x, y, kText, w);
        y += 20;
    }
    // rows: 0 website, 1 e-mail, 2 repository
    if (snap.row_count > 0) {
        draw_text(d, Font::Small, snap.rows[0].text, x, y, accent_color(snap.accent), w, Clip::Middle);
    }
    y += 16;
    if (snap.row_count > 2) {
        draw_text(d, Font::Small, snap.rows[2].text, x, y, kMuted, w, Clip::Middle);
    }
    const int bottom = kContentTop + 78;
    int build_w = 0;
    if (snap.value[0] != '\0') {
        build_w = text_width(d, Font::Small, snap.value);
        draw_text_right(d, Font::Small, snap.value, kWidth - kMargin - 3, bottom, kMuted);
    }
    if (snap.row_count > 1) {
        draw_text(d, Font::Small, snap.rows[1].text, kMargin + 3, bottom, kText,
                  kWidth - 2 * kMargin - 6 - build_w - 8, Clip::Middle);
    }
}

// Long operation. The bar is the single bold element: thick, in the operation's accent, with
// a soft sheen gliding along the filled part so a slow step still visibly works.
template <class D>
void draw_progress(D& d, const Snapshot& snap) {
    const std::uint16_t color = accent_color(snap.accent);
    const int left = kMargin + 2;
    const int width = kWidth - 2 * left;
    draw_text(d, fit_font(d, snap.title, width), snap.title, left, kContentTop + 8, kText, width);
    if (snap.row_count > 0) {
        draw_text(d, Font::Body, snap.rows[0].text, left, kContentTop + 34, kMuted, width);
    }
    constexpr int kBarH = 12;
    const int bar_y = kContentTop + 58;
    d.fillRoundRect(left, bar_y, width, kBarH, kBarH / 2, kSurface);
    const bool indeterminate = snap.progress < 0;
    const int filled = indeterminate ? width : std::clamp(snap.progress, 0, 100) * width / 100;
    if (!indeterminate && filled >= kBarH) {
        d.fillRoundRect(left, bar_y, filled, kBarH, kBarH / 2, color);
    } else if (!indeterminate && filled > 0) {
        d.fillRoundRect(left, bar_y, kBarH, kBarH, kBarH / 2, color);
    }
    // Sheen: a lighter band moving left to right inside the filled part (or the whole track
    // when the length of the work is unknown).
    if (filled > 24) {
        const int band = 22;
        const int x = left + (filled - band) * snap.phase / 99;
        d.fillRoundRect(x, bar_y + 3, band, kBarH - 6, (kBarH - 6) / 2, lighten(color));
    }
    const int info_y = bar_y + kBarH + 5;
    if (snap.row_count > 0 && snap.rows[0].value[0] != '\0') {
        draw_text(d, Font::Small, snap.rows[0].value, left, info_y, kMuted, width / 2);
    }
    if (!indeterminate) {
        char percent[8];
        std::snprintf(percent, sizeof(percent), "%d%%", std::clamp(snap.progress, 0, 100));
        draw_text_right(d, Font::Bold, percent, left + width, info_y - 1, color);
    }
}

// Confirmation: full-bleed so nothing else competes for attention. Safety-critical, so it
// uses only fills, lines and fonts: no animation and no buffering requirement.
template <class D>
void draw_prompt(D& d, const Snapshot& snap) {
    const std::uint16_t color = accent_color(snap.accent);
    d.fillRect(0, 0, kWidth, 24, color);
    draw_icon(d, snap.icon, kMargin, 3, 18, kOnAccent, color);
    draw_text(d, Font::Bold, snap.header != nullptr ? snap.header : "FIDO CONFIRM", kMargin + 26,
              centre_y(d, Font::Bold, 0, 24) - 1, kOnAccent, kWidth - 2 * kMargin - 26);
    d.fillRect(0, 24, kWidth, 3, kSurface);
    if (snap.progress >= 0) {
        d.fillRect(0, 24, std::clamp(snap.progress, 0, 100) * kWidth / 100, 3, color);
    }
    // Vertical budget: band 0-27, title 29-51, rows 54-90, warning 92-108, actions 109-133.
    const Font title_font = fit_font(d, snap.title, kWidth - 2 * kMargin);
    draw_text(d, title_font, snap.title, kMargin, 29, kText, kWidth - 2 * kMargin);
    int y = 54;
    for (int i = 0; i < snap.row_count && i < 2; ++i) {
        draw_text(d, i == 0 ? Font::Bold : Font::Body, snap.rows[i].text, kMargin, y,
                  i == 0 ? kText : kMuted, kWidth - 2 * kMargin, snap.rows[i].clip);
        y += 18;
    }
    if (snap.subtitle[0] != '\0') {
        d.fillRoundRect(kMargin, 92, kWidth - 2 * kMargin, 16, 5, accent_tint(Accent::Amber));
        draw_text(d, Font::Small, snap.subtitle, kMargin + 6, centre_y(d, Font::Small, 92, 16),
                  kWarn, kWidth - 2 * kMargin - 12);
    }
    // Two big actions: what Enter does and what Del does.
    Chip chips[4];
    const int count = parse_footer(snap.footer, chips, 4);
    constexpr int kActionTop = 110;
    constexpr int kActionH = 23;
    const int w = (kWidth - 3 * kMargin) / 2;
    for (int i = 0; i < count && i < 2; ++i) {
        const int x = kMargin + i * (w + kMargin);
        const bool primary = i == 0 && chips[i].key[0] != '\0';
        const std::uint16_t fill = primary ? color : kSurfaceHi;
        const std::uint16_t fg = primary ? kOnAccent : kText;
        d.fillRoundRect(x, kActionTop, w, kActionH, 7, fill);
        int cursor = x + 5;
        if (chips[i].key[0] != '\0') {
            cursor += draw_keycap(d, chips[i].key, cursor, kActionTop + 3, 17,
                                  primary ? kOnAccent : kBg, primary ? color : kMuted) + 6;
        }
        draw_text(d, Font::Bold, chips[i].label, cursor, centre_y(d, Font::Bold, kActionTop, kActionH),
                  fg, x + w - cursor - 6);
    }
}

template <class D>
void draw(D& d, const Snapshot& snap) {
    if (!snap.screen_on) {
        d.fillScreen(0);
        return;
    }
    d.fillScreen(kBg);
    d.setTextWrap(false);
    if (snap.layout == Layout::Prompt) {
        draw_prompt(d, snap);
        return;
    }
    if (snap.layout == Layout::Splash) {
        draw_splash(d, snap);
        draw_footer(d, snap.footer);
        return;
    }
    const std::uint16_t accent = accent_color(snap.accent);
    if (snap.layout == Layout::Progress && snap.header != nullptr) {
        Snapshot bar = snap;   // the bar shows the area, so the task title is not repeated
        bar.title = snap.header;
        draw_status_bar(d, bar, accent);
    } else {
        draw_status_bar(d, snap, accent);
    }
    switch (snap.layout) {
        case Layout::Home:    draw_home(d, snap); break;
        case Layout::List:    draw_list(d, snap); break;
        case Layout::Details: draw_details(d, snap); break;
        case Layout::Editor:  draw_editor(d, snap); break;
        case Layout::Entry:   draw_entry(d, snap); break;
        case Layout::Progress: draw_progress(d, snap); break;
        case Layout::About:    draw_about(d, snap); break;
        case Layout::Splash:   break;
        case Layout::Prompt:  break;
    }
    draw_footer(d, snap.footer);
}

// Full-screen notice for conditions that stop the device: storage damage, halts.
template <class D>
void message(D& d, const char* title, const char* detail) {
    d.fillScreen(kBg);
    d.setTextWrap(false);
    d.fillRect(0, 0, kWidth, 24, kBad);
    draw_icon(d, Glyph::Warn, kMargin, 3, 18, kOnAccent, kBad);
    draw_text(d, Font::Bold, title, kMargin + 26, centre_y(d, Font::Bold, 0, 24) - 1, kOnAccent,
              kWidth - 2 * kMargin - 26);
    // Word-wrap the detail over up to four lines.
    char words[300];
    sanitize(detail, words, sizeof(words));
    int y = 34;
    const char* p = words;
    for (int line = 0; line < 4 && *p != '\0'; ++line) {
        char buffer[80] = {};
        int len = 0;
        const char* last_space = nullptr;
        int space_len = 0;
        const char* start = p;
        while (*p != '\0' && len < 78) {
            if (*p == ' ') { last_space = p; space_len = len; }
            buffer[len++] = *p++;
            buffer[len] = '\0';
            if (text_width(d, Font::Body, buffer) > kWidth - 2 * kMargin) {
                if (last_space != nullptr && last_space != start) {
                    buffer[space_len] = '\0';
                    p = last_space + 1;
                } else {
                    buffer[--len] = '\0';
                    --p;
                }
                break;
            }
        }
        draw_text(d, Font::Body, buffer, kMargin, y, kText);
        y += 21;
    }
}

}  // namespace cardputer::lcd
