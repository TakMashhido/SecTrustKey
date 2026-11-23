// Copyright (c) 2026 Koustav Kumar Mondal. Part of SecTrustKey, licensed under AGPL-3.0.
//
// Setting the clock on the device itself, so on-screen codes work without a computer script.
// Wi-Fi runs only in the pre-USB maintenance boot and the device restarts afterwards, so
// the radio is never on while the security key is reachable over USB.
#pragma once

#include "board.h"

namespace cardputer {

// The user's offset from UTC in minutes (east positive). 0 until chosen. Read once per boot.
int clock_utc_offset_minutes();
// Local hour and minute for the status bar. False while the time is unknown.
bool clock_local_time(int* hour, int* minute);
// "HH:MM" local time, or "Not set".
void clock_label(char* out, unsigned cap);

// Settings > Clock, in the maintenance boot: Wi-Fi sync, network, start-up sync, typing the
// time and the time zone. The caller restarts afterwards.
void clock_menu(Board& board);
// Every normal boot calls this first. With start-up sync on, a saved network and no time
// yet, it joins the network, sets the clock and restarts. Otherwise it returns at once.
void clock_sync_at_start(Board& board);

}  // namespace cardputer
