/*
 * This file is part of the Pico Keys SDK distribution (https://github.com/polhenarejos/pico-keys-sdk).
 * Copyright (c) 2022 Pol Henarejos.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _SERIAL_H_
#define _SERIAL_H_

#include <stdint.h>

#if defined(__APPLE__) || defined(_MSC_VER) || defined(__linux__)
#define DEVICE_UID_SIZE 16
#else
#define DEVICE_UID_SIZE 8
#endif
typedef struct { uint8_t id[DEVICE_UID_SIZE]; } stk_serial_t;

extern stk_serial_t stk_serial;
extern char stk_serial_str[2 * DEVICE_UID_SIZE + 1];
extern uint8_t stk_serial_hash[32];
extern void serial_init(void);

#endif //_SERIAL_H_
