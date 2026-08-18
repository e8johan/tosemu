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

#ifndef AESKERNEL_H
#define AESKERNEL_H

#include <stdint.h>

/*
 * The seam between EmuTOS's AES library files and the AES kernel.
 *
 * The library files - the object, form, resource, graphics and file selector
 * ones - are taken from EmuTOS as they stand, because all they do is call the
 * VDI. The kernel underneath them is not: EmuTOS's is cooperative
 * multitasking with every application in one address space, and tosemu runs
 * an application to a process, so appl_*, evnt_*, wind_*, menu_* and shel_*
 * are written here instead.
 *
 * This is the list of everything the library files reach down for, arrived at
 * by compiling them and seeing what was left over. Anything not here they do
 * not need, and anything here that the kernel stops providing will fail to
 * link rather than quietly misbehave.
 *
 * Nothing in it is implemented yet. Until it is, the definitions in
 * aeskernel.c say so when they are reached.
 */

/* Calling a G_USERDEF object's draw routine, which is 68000 code in the
 * emulated machine rather than anything this side can jump to.
 *
 * Include obdefs.h before this: USERBLK and PARMBLK are its, and they are
 * typedefs rather than tagged structures, so there is no way to name them
 * ahead of it.
 */
WORD host_call_userdef(USERBLK *ub, PARMBLK *pb);

/*
 * Waiting, which is the AES kernel's alone to do. The library files reach it
 * through ev_multi, and ev_multi reaches this: tosemu owns the event queue,
 * the timer and the connection to the compositor, none of which belong on this
 * side of the seam.
 *
 * The flags are the MU_ ones, the timeout is in milliseconds or negative to
 * wait for as long as it takes, and the answer is which of the flags happened.
 * The two rectangles are for the mouse entering or leaving one, and the flag
 * beside each says which of the two is being waited for.
 */
/*
 * Written out in fixed widths rather than in EmuTOS's names, because the two
 * sides of this do not agree about them: a LONG is eight bytes on this side
 * and four on the other, so declaring one here and defining it there puts the
 * arguments in different places and the call comes apart.
 */
/*
 * Reserving the screen a dialog sits on, and giving it back. form_dial does
 * this for an application's dialogs; the alert in gemfmalt.c does it for the
 * AES's own, so that the two behave alike.
 */
/* Putting a message where the application will find it. The AES sends one for
 * a menu selection, for a window that needs redrawing, and for much else. */
void host_message_post(const int16_t *message);

void host_dialog_begin(int16_t x, int16_t y, int16_t width, int16_t height);
void host_dialog_end(void);

/*
 * The same for a menu that has dropped down, which wants a surface of its own
 * for the same reason and a different kind of one: a menu hangs off the bar,
 * has no frame and nothing to drag it by, and is not a window as far as the
 * desktop is concerned.
 */
void host_menu_begin(int16_t x, int16_t y, int16_t width, int16_t height);
void host_menu_end(void);

int16_t host_event_wait(int16_t wanted, int32_t timeout, int16_t *message,
                        const int16_t *m1, int16_t m1flags,
                        const int16_t *m2, int16_t m2flags,
                        int16_t bmask, int16_t bstate,
                        int16_t *key, int16_t *mx, int16_t *my,
                        int16_t *buttons, int16_t *kstate);

#endif /* AESKERNEL_H */
