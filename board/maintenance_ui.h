// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// Small blocking screens for the pre-USB maintenance boot (backup, passphrase, clock):
// they own the display and keyboard until the user answers.
#pragma once

#include "board.h"
#include "dialog.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cardputer {
namespace maint {

inline void pause() {
    vTaskDelay(pdMS_TO_TICKS(10));
}

// Waits for a full release of every key, then returns the next press.
inline Keys fresh_keys(Board& board) {
    do {
        board.poll_input();
        pause();
    } while (board.keys().any);
    for (;;) {
        board.poll_input();
        if (board.keys().any) { return board.keys(); }
        pause();
    }
}

inline void notice(Board& board, const char* title, const char* line, Mood mood = Mood::Info,
                   const char* header = "SD card") {
    board.render(make_dialog(header, title, line, "ENT:OK", mood));
    while (!fresh_keys(board).enter) {}
}

inline bool confirm(Board& board, const char* title, const char* line, Mood mood = Mood::Warn,
                    const char* header = "SD card") {
    board.render(make_dialog(header, title, line, "ENT:YES DEL:NO", mood));
    for (;;) {
        const Keys keys = fresh_keys(board);
        if (keys.del) { return false; }
        if (keys.enter) { return true; }
    }
}

}  // namespace maint
}  // namespace cardputer
