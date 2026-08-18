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

#ifndef GFX_H
#define GFX_H

#include <stdint.h>

struct surface;

/*
 * Showing a surface, and hearing about the keyboard and the mouse.
 *
 * There is no compositor in a test, and there does not need to be: everything
 * here answers that it is not showing anything, and the emulator runs exactly
 * as it did before with the screen only in memory. That is not a fallback so
 * much as the ordinary case for a test suite, and it is what keeps the tests
 * runnable where nobody is logged in.
 */

/* Opens a window, or reports 0 if there is no compositor to open one on */
int gfx_open(struct surface *screen);
void gfx_close();

/* Whether a window is up. Everything below does nothing when it is not. */
int gfx_showing();

/*
 * The connection, for the event loop to wait on beside its timer, or -1 when
 * there is no window. Events are read with gfx_dispatch when it becomes
 * readable, and gfx_flush is what sends anything queued up towards the
 * compositor before waiting.
 */
int gfx_fd();
void gfx_dispatch();
void gfx_flush();

/*
 * Puts what is in the surface on the screen: the planes turned into colours,
 * scaled up by a whole number, and handed over.
 */
void gfx_present();

/* Input ********************************************************************/

/*
 * The keyboard and the mouse, as GEM wants them.
 *
 * A key is one word: the IKBD scan code in the high byte and the character in
 * the low one. An application reads the low byte for text and the high byte
 * for the keys that are not characters, which is why both travel together.
 *
 * The mouse is in surface pixels rather than window ones. The window is a
 * whole number of times larger, and dividing that back out here is what keeps
 * the rest of the emulator from ever having to know it was scaled.
 */

/* Takes the oldest key waiting, or reports 0 if none is */
int gfx_key_take(uint16_t *key);

/* Where the pointer is and which buttons are down, now */
void gfx_mouse(int16_t *x, int16_t *y, int16_t *buttons);

/* Which of shift, control and alt are held, in the bits GEM uses */
uint16_t gfx_kstate();

/*
 * Whether a button has gone up or down since this was last asked. The AES
 * waits for a change rather than for a state, so the change is what it needs
 * to be told about.
 */
int gfx_buttons_changed();

#endif /* GFX_H */
