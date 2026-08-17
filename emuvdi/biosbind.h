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

/* Host replacement for EmuTOS's include/biosbind.h.
 *
 * The BIOS bindings are trap instructions, so the whole header is m68k inline
 * assembly. The VDI reaches only one of them: Setexc, to put its timer routine
 * on the vector that drives vex_timv. Nothing interrupts a hosted VDI, so the
 * vector is remembered and never called.
 */
#ifndef BIOSBIND_H
#define BIOSBIND_H
LONG host_setexc(WORD vecnum, LONG vec);
#define Setexc(vecnum, vec) host_setexc((vecnum), (LONG)(vec))

/* Milliseconds between system timer ticks, which the VDI divides by to turn
 * a vex_timv interval into ticks. 20 is the 50Hz an ST runs at. */
#define Tickcal() (20L)
#endif
