#include "board.h"

#include <cstring>

#include "M5Unified.h"
#include "view.h"

namespace cardputer {
namespace {

std::uint32_t hash_text(std::uint32_t hash, const char* text) {
    while (text != nullptr && *text != '\0') {
        hash ^= static_cast<std::uint8_t>(*text++);
        hash *= 16777619u;
    }
    // Include the terminator: ("A", "BC") and ("AB", "C") are different
    // frames even though their concatenated bytes are identical.
    return hash * 16777619u;
}

M5Canvas frame_canvas(&M5.Display);
bool frame_canvas_ready = false;

bool ensure_frame_canvas() {
    if (frame_canvas_ready) {
        return true;
    }
    // The Cardputer ADV has no PSRAM. Allocate the 240x135 RGB565 buffer from
    // internal RAM once so a full-frame update reaches the panel as one push
    // instead of exposing an intermediate paper-coloured fill.
    frame_canvas.setPsram(false);
    frame_canvas.setColorDepth(16);
    frame_canvas_ready = frame_canvas.createSprite(lcd::kWidth, lcd::kHeight) != nullptr;
    return frame_canvas_ready;
}

}  // namespace

void Board::render(const Snapshot& snapshot) {
    if (!snapshot.screen_on) {
        if (rendered_) {
            M5.Display.fillScreen(TFT_BLACK);
            rendered_ = false;
            last_hash_ = 0;
        }
        return;
    }

    // Every field the view reads must be mixed in, or a changed screen could be skipped as
    // "unchanged".
    std::uint32_t hash = 2166136261u;
    auto mix = [&hash](std::uint32_t value) {
        hash ^= value;
        hash *= 16777619u;
    };
    hash = hash_text(hash, snapshot.title);
    if (snapshot.header != nullptr) {
        hash = hash_text(hash, snapshot.header);
    }
    // Prompts do not draw the toast. Its expiry must not trigger a direct-panel clear and
    // redraw while the user reads a confirmation.
    if (snapshot.layout != Layout::Prompt) {
        hash = hash_text(hash, snapshot.status);
    }
    hash = hash_text(hash, snapshot.subtitle);
    hash = hash_text(hash, snapshot.footer);
    hash = hash_text(hash, snapshot.value);
    mix(static_cast<std::uint32_t>(snapshot.layout));
    mix(static_cast<std::uint32_t>(snapshot.icon));
    mix(static_cast<std::uint32_t>(snapshot.accent));
    mix(static_cast<std::uint32_t>(snapshot.meter));
    mix(static_cast<std::uint32_t>(snapshot.progress));
    mix(static_cast<std::uint32_t>(snapshot.phase));
    mix(static_cast<std::uint32_t>(snapshot.row_count));
    mix(static_cast<std::uint32_t>(snapshot.tile_count));
    mix(static_cast<std::uint32_t>(snapshot.tile_selected));
    const StatusBar& bar = snapshot.bar;
    mix((bar.usb ? 1u : 0u) | (bar.sd ? 2u : 0u) | (bar.locked ? 4u : 0u) |
        (bar.time_known ? 8u : 0u) | (bar.charging ? 16u : 0u));
    mix(static_cast<std::uint32_t>(bar.battery));
    mix(static_cast<std::uint32_t>(bar.hour) << 8 | bar.minute);
    for (int i = 0; i < snapshot.row_count; ++i) {
        const Line& row = snapshot.rows[i];
        hash = hash_text(hash, row.text);
        hash = hash_text(hash, row.sub);
        hash = hash_text(hash, row.value);
        mix(row.selected ? 1u : 0u);
        mix(static_cast<std::uint32_t>(row.icon));
        mix(static_cast<std::uint32_t>(row.accent));
        mix(static_cast<std::uint32_t>(row.clip) << 1 | (row.mono ? 1u : 0u));
    }
    for (int i = 0; i < snapshot.tile_count && i < 8; ++i) {
        const Tile& tile = snapshot.tiles[i];
        hash = hash_text(hash, tile.label);
        hash = hash_text(hash, tile.hint);
        mix(static_cast<std::uint32_t>(tile.glyph));
        mix(static_cast<std::uint32_t>(tile.accent));
        mix(tile.enabled ? 1u : 0u);
    }
    if (rendered_ && hash == last_hash_) {
        return;
    }
    rendered_ = true;
    last_hash_ = hash;

    // Prompt screens are safety-critical and must be visible even if the
    // optional full-frame buffer could not be allocated or pushed. Render
    // them directly; use the buffered path for normal navigation frames.
    if (snapshot.layout != Layout::Prompt && ensure_frame_canvas()) {
        lcd::draw(frame_canvas, snapshot);
        frame_canvas.pushSprite(0, 0);
        return;
    }

    M5.Display.startWrite();
    lcd::draw(M5.Display, snapshot);
    M5.Display.endWrite();
}

void Board::show_message(const char* title, const char* detail) {
    lcd::message(M5.Display, title, detail);
    rendered_ = true;
    last_hash_ = 0;
}

void Board::set_brightness(std::uint8_t percent) {
    if (percent < 10) {
        percent = 10;
    }
    if (percent > 100) {
        percent = 100;
    }
    brightness_ = percent;
    M5.Display.setBrightness(static_cast<std::uint8_t>(percent * 255u / 100u));
}

}  // namespace cardputer
