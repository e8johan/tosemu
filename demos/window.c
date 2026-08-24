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
 * A window, and something drawn in it.
 *
 * The point of this one is what it is not: there is no screen behind it. A GEM
 * application opens a window and the desktop shows a window, with the desktop's
 * own background where an ST would have had grey with an Atari logo on it.
 *
 * It waits for the close box and then goes away, which is what any GEM
 * application does: the AES does not close a window, it tells the application
 * that somebody asked, and the application decides.
 *
 * The same goes for the size box. Taking hold of it starts the drag the desktop
 * would have started had somebody taken hold of a corner, and what comes back
 * is WM_SIZED - a message saying what rectangle the drag arrived at. Nothing
 * has resized yet when that arrives: the application is what decides, and it
 * decides by setting the window's rectangle, which is the two lines below.
 * An application that ignores the message keeps the window it had.
 */

#include <gem.h>
#include <stdio.h>

/*
 * What a window is made of. gemlib names the messages already - WM_REDRAW and
 * WM_CLOSED among them - so only the pieces it leaves out are named here.
 */
#define W_HAS_NAME    (0x0001)
#define W_HAS_CLOSER  (0x0002)
#define W_HAS_MOVER   (0x0008)
#define W_HAS_SIZER   (0x0020)

static short control[5], global[15], intin[16], intout[7];
static long addrin[3], addrout[1];
static AESPB pb = { control, global, intin, intout, addrin, addrout };

static short call_aes(short op, short ni, short no, short ai, short ao)
{
    control[0] = op; control[1] = ni; control[2] = no;
    control[3] = ai; control[4] = ao;
    aes(&pb);
    return intout[0];
}

static short handle, window;

/* Draws what the window holds: a border round the work area and a line of
 * text, redrawn whenever the AES says something uncovered it */
static void draw(void)
{
    short pxy[4], x, y, wide, high;
    char measured[32];

    intin[0] = window;
    intin[1] = 4;                   /* WF_WORKXYWH */
    call_aes(104, 6, 5, 0, 0);
    x = intout[1]; y = intout[2]; wide = intout[3]; high = intout[4];

    vswr_mode(handle, MD_REPLACE);

    /* Clear it, then put something in it */
    vsf_interior(handle, FIS_SOLID);
    vsf_color(handle, 0);
    pxy[0] = x; pxy[1] = y; pxy[2] = x + wide - 1; pxy[3] = y + high - 1;
    v_bar(handle, pxy);

    vsl_color(handle, 1);
    pxy[0] = x + 4;         pxy[1] = y + 4;
    pxy[2] = x + wide - 5;  pxy[3] = y + high - 5;
    v_rbox(handle, pxy);

    vst_color(handle, 1);
    v_gtext(handle, x + 16, y + 24, "A GEM window");
    v_gtext(handle, x + 16, y + 40, "on your own desktop");

    /* How large the work area is, so that dragging the size box shows in the
     * window rather than only round the outside of it */
    sprintf(measured, "%d by %d", wide, high);
    v_gtext(handle, x + 16, y + 56, measured);
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short work_in[11], work_out[57];
    short message[8];
    short i, running;

    appl_init();

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;
    v_opnvwk(work_in, &handle, work_out);

    /* A window with a title, a closer, something to drag it by and a size box
     * to pull it about with */
    intin[0] = W_HAS_NAME|W_HAS_CLOSER|W_HAS_MOVER|W_HAS_SIZER;
    intin[1] = 0; intin[2] = 0; intin[3] = 320; intin[4] = 200;
    window = call_aes(100, 5, 1, 0, 0);              /* wind_create */

    intin[0] = window;
    addrin[0] = (long)"A GEM window";
    intin[1] = 2;                                    /* WF_NAME */
    intin[2] = (short)(((long)"A GEM window") >> 16);
    intin[3] = (short)(((long)"A GEM window") & 0xffff);
    call_aes(105, 6, 1, 0, 0);                       /* wind_set */

    intin[0] = window;
    intin[1] = 20; intin[2] = 20; intin[3] = 260; intin[4] = 140;
    call_aes(101, 5, 1, 0, 0);                       /* wind_open */

    draw();

    /* The loop every GEM application has */
    for (running = 1; running; )
    {
        for (i = 0; i < 16; i++)
            intin[i] = 0;
        intin[0] = 0x0010;                           /* MU_MESAG */
        addrin[0] = (long)message;
        call_aes(25, 16, 7, 1, 0);                   /* evnt_multi */

        switch (message[0])
        {
        case WM_REDRAW:
            draw();
            break;

        /*
         * The rectangle a drag on the size box arrived at. Taking it is what
         * makes the window that size - the AES has changed nothing yet - and
         * the four words are where as well as how large, because a window
         * grown at the bottom right of the screen has to be moved back to fit.
         */
        case WM_SIZED:
            intin[0] = window;
            intin[1] = 5;                            /* WF_CURRXYWH */
            intin[2] = message[4]; intin[3] = message[5];
            intin[4] = message[6]; intin[5] = message[7];
            call_aes(105, 6, 1, 0, 0);               /* wind_set */
            draw();
            break;

        case WM_CLOSED:
            running = 0;
            break;
        }
    }

    intin[0] = window;
    call_aes(102, 1, 1, 0, 0);                       /* wind_close */
    intin[0] = window;
    call_aes(103, 1, 1, 0, 0);                       /* wind_delete */

    v_clsvwk(handle);
    appl_exit();

    printf("closed\n");

    return 0;
}
