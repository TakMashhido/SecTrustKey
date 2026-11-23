// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// Vector icons drawn from primitives on a 24-unit grid and scaled to `size` pixels, so one
// definition serves a 24 px home tile and a 12 px status-bar glyph. `bg` is the colour of
// the surface behind the icon; it is used for cut-outs (keyholes, card chips).
#pragma once

#include <cstdint>

namespace cardputer::lcd {

enum class Glyph : std::uint8_t {
    Key, Codes, Card, Shield, Backup, Settings, Lock, Unlock, Power,
    Volume, Sun, Clock, Trash, Touch, Usb, Battery, Charging, Sd,
    Warn, Check, Info, Cross, Up, Down, Left, Right, Enter, Backspace, Wifi,
};

namespace detail {
// Cardinal and diagonal offsets of radius 10, used for gear teeth and sun rays.
inline constexpr int kRay[8][2] = {{10, 0}, {7, 7}, {0, 10}, {-7, 7},
                                   {-10, 0}, {-7, -7}, {0, -10}, {7, -7}};
}  // namespace detail

template <class Display>
void draw_icon(Display& d, Glyph g, int x, int y, int size, std::uint16_t fg,
               std::uint16_t bg) {
    const int s = size < 8 ? 8 : size;
    auto S = [s](int v) { return (v * s + 12) / 24; };   // grid unit to pixels, rounded
    auto T = [&](int v) { const int t = S(v); return t < 1 ? 1 : t; };  // never vanish
    const int cx = x + s / 2;
    const int cy = y + s / 2;

    switch (g) {
        case Glyph::Key: {
            d.fillCircle(x + S(8), cy, S(6), fg);
            d.fillCircle(x + S(8), cy, S(2), bg);
            d.fillRect(x + S(13), cy - T(1), S(10), T(3), fg);
            d.fillRect(x + S(18), cy, T(2), S(5), fg);
            d.fillRect(x + S(21), cy, T(2), S(4), fg);
            break;
        }
        case Glyph::Codes: {   // a stopwatch: one-time codes live for a short time
            d.fillRoundRect(cx - S(3), y + S(1), S(6), T(2), T(1), fg);     // button
            d.fillRect(cx - T(1), y + S(2), T(2), S(3), fg);                  // stem
            d.fillCircle(cx, cy + S(2), S(9), fg);                            // case
            d.fillCircle(cx, cy + S(2), S(7), bg);                            // face
            d.fillArc(cx, cy + S(2), 0, S(5), 270, 180, fg);                  // time left
            break;
        }
        case Glyph::Card: {
            d.fillRoundRect(x + S(1), y + S(5), S(22), S(15), S(3), fg);
            d.fillRoundRect(x + S(4), y + S(9), S(7), S(6), S(1), bg);
            d.fillRect(x + S(14), y + S(10), S(6), T(2), bg);
            d.fillRect(x + S(14), y + S(14), S(4), T(2), bg);
            break;
        }
        case Glyph::Shield: {
            d.fillRect(x + S(4), y + S(2), S(16), S(10), fg);
            d.fillTriangle(x + S(4), y + S(12), x + S(20), y + S(12), cx, y + S(23), fg);
            d.drawLine(cx - S(4), cy, cx - S(1), cy + S(3), bg);
            d.drawLine(cx - S(4), cy + 1, cx - S(1), cy + S(3) + 1, bg);
            d.drawLine(cx - S(1), cy + S(3), cx + S(5), cy - S(3), bg);
            d.drawLine(cx - S(1), cy + S(3) + 1, cx + S(5), cy - S(3) + 1, bg);
            break;
        }
        case Glyph::Backup: {   // an arrow going down into a tray: "save a copy"
            d.fillRect(cx - T(1), y + S(2), T(3), S(9), fg);
            d.fillTriangle(cx - S(5), y + S(9), cx + S(5), y + S(9), cx, y + S(15), fg);
            d.fillRect(x + S(3), y + S(14), T(3), S(8), fg);
            d.fillRect(x + S(21) - T(3), y + S(14), T(3), S(8), fg);
            d.fillRect(x + S(3), y + S(22) - T(3), S(18), T(3), fg);
            break;
        }
        case Glyph::Sd: {   // an SD card with a cut corner and contacts
            d.fillRect(x + S(5), y + S(2), S(14), S(20), fg);
            d.fillTriangle(x + S(5), y + S(2), x + S(11), y + S(2), x + S(5), y + S(8), bg);
            for (int i = 0; i < 3; ++i) {
                d.fillRect(x + S(11) + i * S(3), y + S(4), T(2), S(5), bg);
            }
            break;
        }
        case Glyph::Settings: {   // gear: six broad teeth round a body with a hole
            d.fillCircle(cx, cy, S(7), fg);
            for (int i = 0; i < 6; ++i) {
                const int a = i * 60 + 30;
                d.fillArc(cx, cy, S(6), S(10), a - 17, a + 17, fg);
            }
            d.fillCircle(cx, cy, S(3), bg);
            break;
        }
        case Glyph::Lock:
        case Glyph::Unlock: {   // a rounded shackle on a body with a keyhole
            const bool open = g == Glyph::Unlock;
            const int sx = open ? cx + S(5) : cx;          // shackle centre
            const int sy = open ? y + S(6) : y + S(8);
            d.fillArc(sx, sy, S(4), S(6), 180, 0, fg);     // the curve
            d.fillRect(sx + S(4), sy, S(2), y + S(11) - sy, fg);           // right leg
            if (!open) {
                d.fillRect(sx - S(6), sy, S(2), y + S(11) - sy, fg);       // left leg
            } else {
                d.fillRect(sx - S(6), sy, S(2), S(2), fg);                 // lifted, short
            }
            d.fillRoundRect(x + S(4), y + S(11), S(16), S(11), S(2), fg);
            d.fillCircle(cx, y + S(15), T(2), bg);
            d.fillRect(cx - T(1) / 2, y + S(15), T(1), S(4), bg);
            break;
        }
        case Glyph::Power: {   // ring open at the top, with the stem through the gap
            d.fillArc(cx, cy + S(1), S(7), S(9), 300, 240, fg);
            d.fillRoundRect(cx - T(1), y + S(1), T(3), S(11), T(1), fg);
            break;
        }
        case Glyph::Volume: {
            d.fillRect(x + S(3), cy - S(3), S(5), S(6), fg);
            d.fillTriangle(x + S(8), cy - S(3), x + S(8), cy + S(3), x + S(14), cy + S(8), fg);
            d.fillTriangle(x + S(8), cy - S(3), x + S(14), cy - S(8), x + S(14), cy + S(8), fg);
            d.fillArc(x + S(14), cy, S(5), S(7), 315, 45, fg);
            d.fillArc(x + S(14), cy, S(9), S(11), 315, 45, fg);
            break;
        }
        case Glyph::Sun: {   // a disc with eight short rays
            d.fillCircle(cx, cy, S(5), fg);
            for (int i = 0; i < 8; ++i) {
                const int a = i * 45;
                d.fillArc(cx, cy, S(7), S(10), a - 7, a + 7, fg);
            }
            break;
        }
        case Glyph::Clock: {
            d.fillCircle(cx, cy, S(10), fg);
            d.fillCircle(cx, cy, S(8), bg);
            d.fillRect(cx - T(1), cy - S(6), T(2), S(7), fg);
            d.fillRect(cx - T(1), cy - T(1), S(5), T(2), fg);
            break;
        }
        case Glyph::Trash: {
            d.fillRoundRect(x + S(5), y + S(8), S(14), S(14), S(2), fg);
            d.fillRect(x + S(3), y + S(5), S(18), T(2), fg);
            d.fillRect(x + S(9), y + S(2), S(6), T(3), fg);
            for (int i = 0; i < 3; ++i) {
                d.fillRect(x + S(8) + i * S(4), y + S(11), T(1), S(8), bg);
            }
            break;
        }
        case Glyph::Touch: {   // concentric rings: "press here"
            d.fillCircle(cx, cy, S(10), fg);
            d.fillCircle(cx, cy, S(8), bg);
            d.fillCircle(cx, cy, S(5), fg);
            d.fillCircle(cx, cy, S(3), bg);
            break;
        }
        case Glyph::Usb: {   // a USB plug: connector with two contacts, neck and cable
            d.fillRoundRect(x + S(6), y + S(2), S(12), S(9), S(1), fg);
            d.fillRect(x + S(8), y + S(5), S(3), S(3), bg);
            d.fillRect(x + S(13), y + S(5), S(3), S(3), bg);
            d.fillRect(x + S(8), y + S(11), S(8), S(5), fg);
            d.fillRect(cx - T(1), y + S(16), T(3), S(7), fg);
            break;
        }
        case Glyph::Battery:
        case Glyph::Charging: {
            d.fillRoundRect(x + S(1), y + S(6), S(19), S(12), S(2), fg);
            d.fillRoundRect(x + S(3), y + S(8), S(15), S(8), S(1), bg);
            d.fillRect(x + S(20), y + S(10), S(3), S(4), fg);
            if (g == Glyph::Charging) {
                // A solid bolt across the cell.
                d.fillTriangle(x + S(13), y + S(7), x + S(6), y + S(13), x + S(11), y + S(13), fg);
                d.fillTriangle(x + S(9), y + S(11), x + S(15), y + S(11), x + S(8), y + S(17), fg);
            }
            break;
        }
        case Glyph::Warn: {
            d.fillTriangle(cx, y + S(2), x + S(1), y + S(21), x + S(23), y + S(21), fg);
            d.fillRect(cx - T(1), y + S(8), T(3), S(7), bg);
            d.fillRect(cx - T(1), y + S(17), T(3), T(3), bg);
            break;
        }
        case Glyph::Check: {
            for (int k = 0; k < 3; ++k) {
                d.drawLine(x + S(4), cy + k, x + S(9), cy + S(5) + k, fg);
                d.drawLine(x + S(9), cy + S(5) + k, x + S(20), y + S(5) + k, fg);
            }
            break;
        }
        case Glyph::Cross: {
            for (int k = 0; k < 3; ++k) {
                d.drawLine(x + S(5), y + S(5) + k, x + S(19), y + S(19) + k, fg);
                d.drawLine(x + S(19), y + S(5) + k, x + S(5), y + S(19) + k, fg);
            }
            break;
        }
        case Glyph::Info: {
            d.fillCircle(cx, cy, S(10), fg);
            d.fillRect(cx - T(1), cy - T(1), T(3), S(7), bg);
            d.fillRect(cx - T(1), cy - S(6), T(3), T(3), bg);
            break;
        }
        case Glyph::Enter: {   // the return-key arrow
            d.fillRect(x + S(17), y + S(4), T(3), S(11), fg);
            d.fillRect(x + S(7), y + S(13), S(13), T(3), fg);
            d.fillTriangle(x + S(2), y + S(14), x + S(9), y + S(8), x + S(9), y + S(20), fg);
            break;
        }
        case Glyph::Backspace: {   // a left-pointing key with a cross
            d.fillTriangle(x + S(1), cy, x + S(8), y + S(5), x + S(8), y + S(19), fg);
            d.fillRect(x + S(8), y + S(5), S(15), S(14), fg);
            for (int k = 0; k < 2; ++k) {
                d.drawLine(x + S(12), cy - S(3) + k, x + S(18), cy + S(3) + k, bg);
                d.drawLine(x + S(18), cy - S(3) + k, x + S(12), cy + S(3) + k, bg);
            }
            break;
        }
        case Glyph::Up:
            d.fillTriangle(cx, y + S(6), x + S(5), y + S(17), x + S(19), y + S(17), fg);
            break;
        case Glyph::Down:
            d.fillTriangle(cx, y + S(18), x + S(5), y + S(7), x + S(19), y + S(7), fg);
            break;
        case Glyph::Left:
            d.fillTriangle(x + S(6), cy, x + S(17), y + S(5), x + S(17), y + S(19), fg);
            break;
        case Glyph::Right:
            d.fillTriangle(x + S(18), cy, x + S(7), y + S(5), x + S(7), y + S(19), fg);
            break;
        case Glyph::Wifi:   // rising signal bars: a wireless network
            for (int i = 0; i < 3; ++i) {
                const int h = S(6 + i * 6);
                d.fillRoundRect(x + S(3 + i * 7), y + S(21) - h, S(4), h, T(1), fg);
            }
            break;
    }
}

}  // namespace cardputer::lcd
