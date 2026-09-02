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

/* vq_gdos, which is not a call so much as a knock on the door: a program puts
 * -2 in d0 and asks whether anybody answers. TOS itself does not - its trap
 * handler knows the two numbers above and returns without touching d0 for
 * anything else - so d0 coming back as -2 is what "no GDOS" looks like, and
 * GDOS being installed means something has taken the trap over and left a
 * version number there instead.
 * http://toshyp.atari.org/en/006.html
 */
#define GEM_GDOS (-2)

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
/* The screen this build makes when there is no daemon to say otherwise */
void gem_default_screen(int16_t *width, int16_t *height, int16_t *planes);

int gem_start();

/* Lets go of everything a child of fork inherited and should not use: the
 * compositor's connection, the daemon's socket, and the parent's screen */
void gem_forget(void);

/*
 * Reserves the screen for a dialog and gives it back. A dialog draws into a
 * surface of its own and is shown in a window of its own, which is why what it
 * draws does not also appear in the window behind it.
 */
void gem_dialog_begin(int16_t x, int16_t y, int16_t width, int16_t height);
void gem_dialog_end();

/* Puts what has been drawn where it can be seen: a window if there is a
 * compositor, and a file if TOSEMU_SCREENSHOT asked for one */
void gem_present();

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
