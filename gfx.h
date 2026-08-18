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
 * Showing GEM's windows, and hearing about the keyboard and the mouse.
 *
 * The emulated screen is never shown. It is a coordinate space and a piece of
 * memory: the AES lays windows out in it and everything is drawn into it, the
 * way it always was, and an application cannot tell the difference. What the
 * person watching sees are the windows - each GEM window is a window of the
 * desktop's, and so is each dialog - with the desktop itself showing through
 * where an ST would have had a grey background with an Atari logo on it.
 *
 * That is the point of the whole exercise: a GEM application should be part of
 * the desktop it is running on rather than a picture of another computer.
 *
 * There is no compositor in a test, and there does not need to be: everything
 * here answers that it is not showing anything, and the emulator runs exactly
 * as it did before with the screen only in memory.
 */

/* Connects to the compositor, or reports 0 if there is nothing to connect to.
 * Opens no window: there is nothing to show until GEM opens something. */
int gfx_open(struct surface *screen);
void gfx_close(void);

/* Lets go of a connection this process inherited by being forked, without
 * tearing down what belongs to the parent */
void gfx_forget(void);

/* Whether a window is up. Everything below does nothing when it is not. */
int gfx_showing();

/*
 * A GEM window, shown as a window of the desktop's.
 *
 * The handle is the AES's, so that closing one closes the right window. What
 * is shown is that rectangle of the screen - the whole window including the
 * frame GEM draws round it, because that frame is part of what the application
 * put there.
 */
void gfx_window_open(int16_t handle, const char *title, int16_t x, int16_t y,
                     int16_t w, int16_t h);
void gfx_window_move(int16_t handle, int16_t x, int16_t y,
                     int16_t w, int16_t h);
void gfx_window_title(int16_t handle, const char *title);
void gfx_window_close(int16_t handle);

/*
 * A dialog: a window of its own showing a rectangle of the surface a dialog
 * draws into, marked modal and belonging to whichever GEM window is on top, so
 * that the desktop gives it the treatment a dialog gets - kept above that
 * window, and that window kept out of reach while it is up.
 *
 * It stays movable. Nothing here knows or cares where the desktop puts it:
 * what an application sees is the rectangle it asked for, wherever that
 * rectangle happens to be shown, so dragging the window about changes nothing
 * the application can observe.
 */
/*
 * A menu that has dropped down: a popup belonging to the bar, showing that
 * rectangle of the screen. It has no frame, cannot be moved and is not a
 * window as far as the desktop is concerned, which is what a menu should be.
 */
void gfx_menu_open(int16_t x, int16_t y, int16_t w, int16_t h);
void gfx_menu_close(void);

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

/* Whether the pointer has been anywhere yet. Where it is means nothing until
 * it has: nought,nought is a real place, and things happen there. */
int gfx_mouse_known(void);
uint16_t gfx_kstate();
/*
 * The next time the buttons changed, with where the pointer was when they did,
 * or 0 if they have not changed since this was last asked.
 *
 * Changes are kept rather than only the state, because the AES waits for a
 * press and then for the release and works out what was clicked from the two.
 * A click quick enough that both arrive before anyone looks would otherwise
 * read as a button that is up, and the press it was waiting for would never
 * have happened.
 */
int gfx_button_take(int16_t *buttons, int16_t *x, int16_t *y);

/* Whether the pointer moved without anything being pressed. Only injected
 * input produces these; a real pointer is simply somewhere when asked. */
int gfx_motion_take(void);

#endif /* GFX_H */
