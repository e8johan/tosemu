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

/*
 * The emulator, for a test that has none.
 *
 * The ported VDI and the parts of the AES beside it reach back into tosemu in
 * a handful of places: to open a window for a dialog, to send an application a
 * message, to wait for an event, to run a routine the application supplied, to
 * ask which drives there are and how a path is spelled. Every one of those
 * needs a 68000 with a program in it, and the host built tests have neither -
 * they are the port compiled against the host and nothing else.
 *
 * So this answers them, and the answers are the truthful ones for a machine
 * with no application in it rather than plausible ones: no drives, no such
 * directory, nothing to draw with. The two that should never be reached at all
 * say so on the way past, because a test that quietly waited for an event
 * nobody can deliver would look like a hang rather than a mistake.
 *
 * It is one file rather than a block in each test because there are two tests
 * now - bin/vditest and bin/gdostest - and a second copy of this would be a
 * second thing to keep in step with the seam it stands in for.
 */

#include <stdint.h>
#include <stdio.h>

#include "emuvdi.h"

void host_message_post(const int16_t *message)
{
    (void)message;
}

void host_dialog_begin(int16_t x, int16_t y, int16_t width, int16_t height)
{
    (void)x; (void)y; (void)width; (void)height;
}

void host_dialog_end(void)
{
}

void host_menu_surface(void)
{
}

void host_menu_begin(int16_t x, int16_t y, int16_t width, int16_t height)
{
    (void)x; (void)y; (void)width; (void)height;
}

void host_menu_end(void)
{
}

/*
 * tosemu's own, which these tests do not link. A directory that cannot be
 * found and a machine with no drives are what a program with no filesystem
 * should be told.
 */
int32_t tos_path_to_host(const char *tos_path, char *host_path)
{
    (void)tos_path;

    if (host_path)
        host_path[0] = 0;

    return -34;         /* EPTHNF */
}

uint32_t drive_map(void)
{
    return 0;
}

/*
 * Drawing an object the application draws itself, which means running the
 * emulated CPU. There is none here, so it says so and leaves the object as it
 * was.
 */
int16_t host_userdef_draw(const struct host_userdef *call)
{
    fprintf(stderr, "an object drawn by the application, and this test has no "
            "machine to run its routine in\n");

    return call->currstate;
}

int aes_userdef_running(void)
{
    return 0;
}

/*
 * The AES kernel's waiting, which the object library is linked against and
 * nothing here reaches. It says so rather than returning something that could
 * be mistaken for an event.
 */
int16_t host_event_wait(int16_t wanted, int32_t timeout, int16_t *message,
                        const int16_t *m1, int16_t m1flags,
                        const int16_t *m2, int16_t m2flags,
                        int16_t *key, int16_t *mx, int16_t *my,
                        int16_t *buttons, int16_t *kstate)
{
    (void)wanted; (void)timeout; (void)message;
    (void)m1; (void)m1flags; (void)m2; (void)m2flags;
    (void)key; (void)mx; (void)my; (void)buttons; (void)kstate;

    fprintf(stderr, "something waited for an event, and this test has no way "
            "to deliver one\n");

    return 0;
}
