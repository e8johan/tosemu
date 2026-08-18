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
 * Showing the screen, and hearing about the keyboard and the mouse.
 *
 * What is shown is not the screen but rectangles of it. There is one for the
 * screen itself, and a dialog gets another, so that a GEM dialog is a window
 * of the compositor's rather than a picture of one drawn inside a bigger
 * window. The AES goes on drawing everything into one flat screen either way,
 * which is what lets an application be unaware of any of this.
 *
 * There is no compositor in a test, and there does not need to be: everything
 * here answers that it is not showing anything, and the emulator runs exactly
 * as it did before with the screen only in memory.
 */

/* Opens the window the screen is shown in, or reports 0 if there is no
 * compositor to open one on */
int gfx_open(struct surface *screen);
void gfx_close();

/* Whether a window is up. Everything below does nothing when it is not. */
int gfx_showing();

/*
 * A dialog: a window of its own showing that rectangle of the screen, marked
 * modal and belonging to the main window, so that the compositor gives it the
 * treatment a dialog gets - kept above its parent, and the parent kept out of
 * reach while it is up.
 *
 * It stays movable. Nothing here knows or cares where the compositor puts it:
 * what an application sees is the rectangle it asked for, wherever that
 * rectangle happens to be shown, so dragging the window about changes nothing
 * the application can observe.
 */
void gfx_dialog_open(struct surface *shows, int16_t x, int16_t y,
                     int16_t w, int16_t h);
void gfx_dialog_close();

/*
 * The connection, for the event loop to wait on beside its timer, or -1 when
 * there is no window.
 */
int gfx_fd();
void gfx_dispatch();
void gfx_flush();

/* Puts what is in the surface on the screen, in every window showing part
 * of it */
void gfx_present();

/* Input ********************************************************************/

/*
 * The keyboard and the mouse, as GEM wants them.
 *
 * A key is one word: the IKBD scan code in the high byte and the character in
 * the low one.
 *
 * The mouse is in the screen's own pixels. A window shows a rectangle of the
 * screen at some whole-number scale, so a pointer position is divided by the
 * scale and moved by where that rectangle starts - which is what keeps every
 * other part of the emulator from having to know a window was involved.
 */
int gfx_key_take(uint16_t *key);
void gfx_mouse(int16_t *x, int16_t *y, int16_t *buttons);
uint16_t gfx_kstate();
int gfx_buttons_changed();

#endif /* GFX_H */
