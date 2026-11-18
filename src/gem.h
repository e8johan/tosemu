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

#ifndef GEM_H
#define GEM_H

#include <stdint.h>

/* GEM, which is the AES and the VDI together.
 *
 * Both halves arrive through trap #2, and which one is being called is told
 * apart by a magic number in d0 rather than by the function number, so the two
 * need a shared front door. That is all this module is: the demultiplexer, and
 * the handful of things the two halves have in common.
 */

/* Serves a trap #2, dispatching to whichever half of GEM was asked for */
void gem_trap();

/*
 * The screen memory's reads and writes, which are the screen GEM shows while
 * GEM is up - see gem.c. tossystem.c puts them on the memory area.
 */
struct _memarea;
uint8_t gem_screen_read(struct _memarea *area, uint32_t address);
void gem_screen_write(struct _memarea *area, uint32_t address, uint8_t value);

/* Drops everything both halves hold for the running application.
 *
 * An application replacing another one gets a GEM that has never been spoken
 * to, in the same way that it gets system RAM that was never handed out - see
 * xbios_reset.
 */
void gem_reset();

#endif /* GEM_H */
