// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// Visual identity of the on-device interface: palette, accents and type sizes.
// The panel is 240x135 RGB565, viewed at arm's length, so contrast and size matter more
// than ornament. Every text/background pair used by the views meets a 4.5:1 contrast
// ratio; measure a new colour before using it.
#pragma once

#include <cstdint>

namespace cardputer::lcd {

constexpr int kWidth = 240;
constexpr int kHeight = 135;

// 8-bit RGB to RGB565, usable in constant expressions.
constexpr std::uint16_t rgb(unsigned r, unsigned g, unsigned b) {
    return static_cast<std::uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

// Surfaces, darkest to lightest.
inline constexpr std::uint16_t kBg = rgb(0x0A, 0x0F, 0x1E);       // screen background
inline constexpr std::uint16_t kBar = rgb(0x12, 0x19, 0x30);      // status bar, footer
inline constexpr std::uint16_t kSurface = rgb(0x1A, 0x23, 0x42);  // cards, rows
inline constexpr std::uint16_t kSurfaceHi = rgb(0x26, 0x33, 0x5E);// selected row, pressed
inline constexpr std::uint16_t kLine = rgb(0x2E, 0x3A, 0x63);     // hairlines

// Text.
inline constexpr std::uint16_t kText = rgb(0xF2, 0xF5, 0xFF);     // primary
inline constexpr std::uint16_t kMuted = rgb(0xA3, 0xAE, 0xCF);    // secondary
inline constexpr std::uint16_t kOnAccent = rgb(0x06, 0x0B, 0x18); // text on a bright accent

// Signals.
inline constexpr std::uint16_t kGood = rgb(0x3D, 0xDC, 0x84);
inline constexpr std::uint16_t kWarn = rgb(0xFF, 0xB0, 0x20);
inline constexpr std::uint16_t kBad = rgb(0xFF, 0x5C, 0x7A);

// One hue per application, so the user can tell what is asking at a glance.
enum class Accent : std::uint8_t {
    Teal,     // FIDO2 / passkeys, the product accent
    Violet,   // OpenPGP
    Amber,    // PIV
    Rose,     // one-time codes
    Blue,     // system, settings
    Green,    // healthy / backup
    Red,      // destructive
};

constexpr std::uint16_t accent_color(Accent a) {
    switch (a) {
        case Accent::Violet: return rgb(0xA8, 0x8B, 0xFF);
        case Accent::Amber:  return rgb(0xFF, 0xB0, 0x20);
        case Accent::Rose:   return rgb(0xFF, 0x7A, 0xB6);
        case Accent::Blue:   return rgb(0x5C, 0xA8, 0xFF);
        case Accent::Green:  return rgb(0x3D, 0xDC, 0x84);
        case Accent::Red:    return rgb(0xFF, 0x5C, 0x7A);
        case Accent::Teal:
        default:             return rgb(0x2E, 0xE6, 0xC5);
    }
}

// A dim tint of the accent, for chips and selected backgrounds.
constexpr std::uint16_t accent_tint(Accent a) {
    switch (a) {
        case Accent::Violet: return rgb(0x2A, 0x24, 0x4F);
        case Accent::Amber:  return rgb(0x40, 0x2E, 0x10);
        case Accent::Rose:   return rgb(0x45, 0x20, 0x35);
        case Accent::Blue:   return rgb(0x1B, 0x2F, 0x55);
        case Accent::Green:  return rgb(0x12, 0x3D, 0x2C);
        case Accent::Red:    return rgb(0x48, 0x1C, 0x2A);
        case Accent::Teal:
        default:             return rgb(0x10, 0x3A, 0x3A);
    }
}

// Halfway between a colour and white: a soft highlight (the progress sheen).
constexpr std::uint16_t lighten(std::uint16_t c) {
    const unsigned r = ((c >> 11) & 31u) + 31u;
    const unsigned g = ((c >> 5) & 63u) + 63u;
    const unsigned b = (c & 31u) + 31u;
    return static_cast<std::uint16_t>(((r / 2) << 11) | ((g / 2) << 5) | (b / 2));
}

// How an over-long string is shortened. A site name keeps its END, because the registrable
// domain is what identifies the site (`paypal.com.evil.example` must not collapse to
// `paypal.com.ev...`); an account keeps both ends; anything else keeps its start.
enum class Clip : std::uint8_t { Tail, Head, Middle };

// Layout metrics shared by every screen.
inline constexpr int kBarHeight = 18;
inline constexpr int kFooterHeight = 18;
inline constexpr int kContentTop = kBarHeight + 1;
inline constexpr int kContentBottom = kHeight - kFooterHeight - 1;  // exclusive
inline constexpr int kMargin = 6;

}  // namespace cardputer::lcd
