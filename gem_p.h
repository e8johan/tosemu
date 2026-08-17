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

#ifndef GEM_P_H
#define GEM_P_H

#include <stdint.h>

/* Private to the GEM namespace: gem.c, aes.c and vdi.c.
 *
 * This header deliberately does not pull in config.h. The AES and the VDI each
 * define their own trace context before including it, and config.h has no
 * include guard, so a file that reached both would define the tracing macros
 * twice.
 */

/* Which half of GEM a trap #2 is for, in d0. The AES and the VDI have
 * overlapping function numbers, so this is the only thing telling them apart.
 * http://toshyp.atari.org/en/aes.html
 */
#define GEM_AES (200) /* 0xc8 */
#define GEM_VDI (115) /* 0x73 */

/* How a function the tables name but nobody has implemented behaves, the same
 * contract BIOS and XBIOS use. FN_HALT stops and says which call it was, so
 * that an unimplemented function is found where it is used rather than
 * silently doing nothing. FN_STUB is for the calls that have a documented
 * answer meaning "that did not happen", which can be given without pretending
 * to do the work.
 */
#define FN_HALT (0)
#define FN_STUB (1)

/* Readies GEM the first time either half is asked for anything. Returns 0
 * after saying why not, having halted. */
int gem_start();

/* The two halves, called from gem_trap */
void aes_trap();
void vdi_trap();
void aes_reset();
void vdi_reset();

/* The parameter blocks of both halves are arrays of words in the emulated
 * memory. These read and write them in host endianess, and are bounds
 * checked against the array the caller declared: an application that says it
 * passed two words and a function that reads a third would otherwise read
 * whatever happened to follow.
 */
int16_t gem_word(uint32_t array, int count, int index);
void gem_set_word(uint32_t array, int count, int index, int16_t value);
uint32_t gem_long(uint32_t array, int count, int index);
void gem_set_long(uint32_t array, int count, int index, uint32_t value);

#endif /* GEM_P_H */
