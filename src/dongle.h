/*
 * TOSEMU - an emulated environment for TOS applications
 * Copyright (C) 2026 Johan Toverland Thelin <e8johan@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef DONGLE_H
#define DONGLE_H

#include <stdint.h>

#include "memory.h"

/*
 * The ROM cartridge port, which is the one part of an ST that tosemu has never
 * had anything at. A hundred and twenty eight kilobytes at the top of memory,
 * below the hardware registers, that a machine answers nothing for when there
 * is nothing plugged into it - which is why reading it halted the emulator
 * rather than returning the 0xff an empty port really gives.
 */
#define CARTRIDGE_BASE_ADDRESS (0xFA0000)
#define CARTRIDGE_LENGTH       (0x20000)

/* What was asked for on the command line, if anything. Returns 0 for a name
 * this does not know, having said which names it does */
int         dongle_asked_for(const char *name);
int         dongle_wanted(void);
const char *dongle_named(void);

/*
 * The key itself, as the three things a registered state machine is: what it
 * holds, what it answers with, and the edge that moves it on. Kept apart from
 * the memory area below so that the equations can be checked on their own -
 * see src/dongletest.c, which is the only reason any of this is separable.
 */
void dongle_reset(void);
void dongle_clock(int a8);
int  dongle_answer(void);

/* The cartridge port as the machine sees it. Registered by tossystem.c, which
 * is where the memory areas are put up, and only when a key was asked for */
uint8_t dongle_area_read(struct _memarea *area, uint32_t address);
void    dongle_area_write(struct _memarea *area, uint32_t address,
                          uint8_t value);

#endif
