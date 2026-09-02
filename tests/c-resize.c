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
 * A window that has been given a new rectangle, which is what an application
 * does when it answers WM_SIZED.
 *
 * Two things have to happen and neither is the resizing itself, which is the
 * application's own doing. The frame has to move with the window - a scroll
 * bar left at the old edge is a bar down the middle of a document - and the
 * application has to be told to paint what is now inside, because a window
 * that has changed shape has parts nobody has drawn in: some of them were
 * outside it a moment ago, and the rest are where the frame used to be.
 *
 * The drag that produces the new rectangle is the compositor's and cannot be
 * arranged from here. What can be is the half after it: the application asks
 * for the new rectangle and this is what has to follow.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <gem.h>

/* What a window is made of, http://toshyp.atari.org/en/008009.html */
#define W_HAS_NAME    (0x0001)
#define W_HAS_CLOSER  (0x0002)
#define W_HAS_MOVER   (0x0008)
#define W_HAS_SIZER   (0x0020)

/* How long to wait for a message that has to be there: long enough that a slow
 * machine is not what decides, and short enough that a missing one is reported
 * rather than waited for. gemlib names the two kinds of wait already. */
#define A_MOMENT      (500)

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

static short message[8];

/* The next message, or 0 when none arrives before the moment is up */
static short next_message(void)
{
    short i;

    for (i = 0; i < 16; i++)
        intin[i] = 0;

    intin[0] = MU_MESAG|MU_TIMER;
    intin[14] = A_MOMENT;                            /* the low word of it */
    addrin[0] = (long)message;

    /* All of it, so that nothing of the last message can answer for this one:
     * a check on which window changed shape is worth nothing if the answer is
     * left over from the message before */
    for (i = 0; i < 8; i++)
        message[i] = 0;

    if (!(call_aes(25, 16, 7, 1, 0) & MU_MESAG))     /* evnt_multi */
        return 0;

    return message[0];
}

static void set_rectangle(short window, short x, short y, short w, short h)
{
    intin[0] = window;
    intin[1] = 5;                                    /* WF_CURRXYWH */
    intin[2] = x; intin[3] = y; intin[4] = w; intin[5] = h;
    call_aes(105, 6, 1, 0, 0);                       /* wind_set */
}

static void work_area(short window, short *x, short *y, short *w, short *h)
{
    intin[0] = window;
    intin[1] = 4;                                    /* WF_WORKXYWH */
    call_aes(104, 6, 5, 0, 0);                       /* wind_get */

    *x = intout[1]; *y = intout[2]; *w = intout[3]; *h = intout[4];
}

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

/* Whether anything is drawn in a band of the screen */
static int ink_in(short handle, short x, short y, short wide, short high)
{
    short value, index;
    short i, j;

    for (j = 0; j < high; j++)
        for (i = 0; i < wide; i++)
        {
            v_get_pixel(handle, (short)(x + i), (short)(y + j),
                        &value, &index);
            if (index != 0)
                return 1;
        }

    return 0;
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short work_in[11], work_out[57];
    short handle;
    short i;
    short window;
    short wx, wy, wide = 300, high = 150;
    short grown_w = 500, grown_h = 250;
    short work_x, work_y, work_w, work_h;
    short was_w;

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;
    v_opnvwk(work_in, &handle, work_out);

    wx = 0;
    wy = hbox;

    intin[0] = W_HAS_NAME|W_HAS_CLOSER|W_HAS_MOVER|W_HAS_SIZER;
    intin[1] = wx; intin[2] = wy; intin[3] = wide; intin[4] = high;
    window = call_aes(100, 5, 1, 0, 0);              /* wind_create */

    intin[0] = window;
    intin[1] = wx; intin[2] = wy; intin[3] = wide; intin[4] = high;
    call_aes(101, 5, 1, 0, 0);                       /* wind_open */

    /* Opening one asks for it to be painted as well, and that message is not
     * the one this is about */
    check(next_message(), WM_REDRAW, "opening a window asks for it to be drawn");

    work_area(window, &work_x, &work_y, &work_w, &work_h);
    was_w = work_w;

    check(ink_in(handle, (short)(work_x + work_w), (short)(work_y + 4), 1, 4), 1,
          "the scroll bar is down the right hand edge of the work area");

    /*
     * And now the window is given a larger rectangle, which is what an
     * application does with the four words WM_SIZED carries.
     */
    set_rectangle(window, wx, wy, grown_w, grown_h);

    check(next_message(), WM_REDRAW,
          "a window given a new rectangle is asked to be drawn again");

    check(message[3], window, "and it is the window that changed shape");
    check(message[6], grown_w, "over the whole of its new width");
    check(message[7], grown_h, "and its new height");

    /* The frame went with it: the bar is down the new edge rather than the
     * old one, which is now somewhere in the middle of a wider window */
    work_area(window, &work_x, &work_y, &work_w, &work_h);

    check(work_w > was_w, 1, "the work area grew with the window");

    check(ink_in(handle, (short)(work_x + work_w), (short)(work_y + 4), 1, 4), 1,
          "and the scroll bar moved to the new edge with it");

    /*
     * Set to the rectangle it already has, which is what answering WM_SIZED
     * with nothing new amounts to. Nothing changed, so there is nothing to
     * draw again and no message: an application asked to redraw a document
     * that has not moved would do the work for nobody.
     */
    set_rectangle(window, wx, wy, grown_w, grown_h);

    check(next_message(), 0,
          "a window set to the rectangle it has is not asked to draw again");

    printf("1..%d\n", n);

    intin[0] = window;
    call_aes(102, 1, 1, 0, 0);                       /* wind_close */
    intin[0] = window;
    call_aes(103, 1, 1, 0, 0);                       /* wind_delete */

    v_clsvwk(handle);
    appl_exit();

    return 0;
}
