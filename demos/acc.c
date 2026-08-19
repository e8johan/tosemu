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
 * A desk accessory.
 *
 * The difference between one of these and an ordinary program is not what it
 * is made of - it is a GEM application like any other - but that it never
 * stops. A program runs, does something and exits; an accessory says what it
 * is called, and then waits for the rest of the session to ask for it.
 *
 * Saying what it is called is menu_register, and it is the only thing that
 * makes an accessory reachable: the name goes into the Desk menu of every
 * application that is running, including the ones that started first and the
 * ones that have not started yet. Picking it there sends this program AC_OPEN,
 * and that is its cue to put a window up.
 *
 * So the loop below is the whole shape of an accessory. It never falls out of
 * it, because there is nothing for an accessory to exit to.
 *
 * Run it by putting it in a directory and pointing the daemon at that:
 *
 *     make demos
 *     mkdir -p /tmp/acc && cp demos/DEMO.ACC /tmp/acc/
 *     bin/tosaesd -v /tmp/acc &
 *     bin/tosemu demos/menu
 *
 * and the Desk menu of the menu demo has Demo in it, under a separator.
 */

#include <gem.h>
#include <stdio.h>

static short control[5], global[15], intin[16], intout[7];
static long addrin[3], addrout[1];
static AESPB pb = { control, global, intin, intout, addrin, addrout };

static short call_aes(short op, short ni, short no, short ai, short ao)
{
    control[0] = op; control[1] = ni; control[2] = no;
    control[3] = ai; control[4] = ao;
    intout[0] = -1;
    aes(&pb);
    return intout[0];
}

int main(int argc, char **argv)
{
    short id, entry, handle, wchar, hchar, wbox, hbox;
    short message[8];
    short opened = 0;

    id = appl_init();
    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);

    /*
     * The name, which is what appears in the Desk menu. Padding it out is
     * worth doing rather than leaving to chance: the menu is as wide as its
     * widest entry, and a name with nothing round it sits against the edge.
     */
    addrin[0] = (long)"  Demo  ";
    intin[0] = id;
    entry = call_aes(35, 1, 1, 1, 0);            /* menu_register */

    if (entry < 0)
    {
        printf("the AES would not have it as an accessory\n");
        return 1;
    }

    printf("registered as application %d, menu entry %d\n", id, entry);
    fflush(stdout);

    for (;;)
    {
        evnt_mesag(message);

        switch (message[0])
        {
        case AC_OPEN:
            /*
             * Somebody picked it. A real accessory opens a window here, and
             * keeps it until it is told to close - which is the one thing it
             * must not do by itself, because the window belongs to a session
             * that is still running.
             */
            opened++;
            printf("opened, %d time%s so far\n", opened,
                   (opened == 1) ? "" : "s");
            fflush(stdout);
            break;

        case AP_TERM:
            /*
             * The session is ending. This is the one message an accessory
             * does fall out of its loop for: not the application closing,
             * which happens all day, but the thing that started it saying
             * there will not be another.
             */
            printf("asked to go, and going\n");
            fflush(stdout);
            v_clsvwk(handle);
            appl_exit();
            return 0;

        case AC_CLOSE:
            /*
             * The application that was running has stopped, and everything on
             * the screen went with it. An accessory is told so that it can
             * forget the window it had rather than go on believing in one.
             */
            printf("told to close\n");
            fflush(stdout);
            break;
        }
    }

    /* Not reached, and that is the point of an accessory */
}
