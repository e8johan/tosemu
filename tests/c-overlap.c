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
 * Two windows that overlap, and whose pixels are whose where they do.
 *
 * Each window keeps a picture of its own here, because on a desktop the two
 * are separate windows that somebody can put anywhere and each has to hold all
 * of its own picture. While they shared one screen they shared the rectangle
 * they overlapped in, and whichever had drawn last was in both of them:
 * Microsoft Write's Show Clipboard window appeared inside the document window
 * underneath it as well as in its own.
 *
 * What a program can see of that is what v_get_pixel answers, which is what an
 * Atari would have had on the screen: the window in front, where they overlap.
 * So the back window is brought to the front without anything being redrawn,
 * and asked again. Its own picture has to still be there - that is the whole
 * of the difference, and with one screen between them it could not be, because
 * the front window had drawn over it.
 *
 * Everything here is drawn the way GEM applications draw: the window is asked
 * which parts of it are showing and each is clipped to before anything is put
 * in it. That is also what says which window the drawing is for - see
 * drawing_for in aeswind.c - so a test that drew any other way would be
 * checking something no application does.
 */

#include <stdio.h>
#include <gem.h>

#define W_HAS_NAME    (0x0001)
#define W_HAS_CLOSER  (0x0002)
#define W_HAS_MOVER   (0x0008)

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

static int n;
static int fails;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
    }
}

static short handle;

/* What is on the screen at a point, which is whichever window is in front
 * there - the question an application asks, and the answer an Atari gave */
static short pen_at(short x, short y)
{
    short value, index;

    v_get_pixel(handle, x, y, &value, &index);

    return index;
}

/* Opens a window at a rectangle and answers its handle */
static short open_window(const char *name, short x, short y, short w, short h)
{
    short window;

    intin[0] = W_HAS_NAME|W_HAS_CLOSER|W_HAS_MOVER;
    intin[1] = x; intin[2] = y; intin[3] = w; intin[4] = h;
    window = call_aes(100, 5, 1, 0, 0);              /* wind_create */

    if (window <= 0)
        return 0;

    intin[0] = window;
    intin[1] = 2;                                    /* WF_NAME */
    intin[2] = (short)(((long)name) >> 16);
    intin[3] = (short)(((long)name) & 0xffff);
    call_aes(105, 6, 1, 0, 0);                       /* wind_set */

    intin[0] = window;
    intin[1] = x; intin[2] = y; intin[3] = w; intin[4] = h;
    call_aes(101, 5, 1, 0, 0);                       /* wind_open */

    return window;
}

/*
 * Fills a window's work area with one pen, a rectangle of it at a time.
 *
 * The rectangle list is asked for and clipped to even though nothing here
 * covers anything - the AES answers that the whole work area is showing,
 * every window having a picture of its own - because that is how a GEM
 * application redraws and what the emulator listens to.
 */
static void fill_window(short window, short pen)
{
    short pxy[4];
    short x, y, w, h;

    intin[0] = window;
    intin[1] = 4;                                    /* WF_WORKXYWH */
    call_aes(104, 6, 5, 0, 0);                       /* wind_get */
    x = intout[1]; y = intout[2]; w = intout[3]; h = intout[4];

    intin[0] = 1;                                    /* BEG_UPDATE */
    call_aes(107, 1, 1, 0, 0);                       /* wind_update */

    intin[0] = window;
    intin[1] = 11;                                   /* WF_FIRSTXYWH */
    call_aes(104, 6, 5, 0, 0);

    while (intout[3] > 0 && intout[4] > 0)
    {
        pxy[0] = intout[1];
        pxy[1] = intout[2];
        pxy[2] = (short)(intout[1] + intout[3] - 1);
        pxy[3] = (short)(intout[2] + intout[4] - 1);
        vs_clip(handle, 1, pxy);

        vswr_mode(handle, MD_REPLACE);
        vsf_interior(handle, FIS_SOLID);
        vsf_color(handle, pen);

        pxy[0] = x; pxy[1] = y;
        pxy[2] = (short)(x + w - 1); pxy[3] = (short)(y + h - 1);
        v_bar(handle, pxy);

        intin[0] = window;
        intin[1] = 12;                               /* WF_NEXTXYWH */
        call_aes(104, 6, 5, 0, 0);
    }

    pxy[0] = 0; pxy[1] = 0; pxy[2] = 0; pxy[3] = 0;
    vs_clip(handle, 0, pxy);

    intin[0] = 0;                                    /* END_UPDATE */
    call_aes(107, 1, 1, 0, 0);
}

/* The middle of a window's work area, which is where it is certainly the
 * window's own drawing rather than the frame the AES puts round it */
static void inside(short window, short *x, short *y)
{
    intin[0] = window;
    intin[1] = 4;                                    /* WF_WORKXYWH */
    call_aes(104, 6, 5, 0, 0);

    *x = (short)(intout[1] + intout[3] / 2);
    *y = (short)(intout[2] + intout[4] / 2);
}

/* And a point down the left of one, which for the window behind is work area
 * the window in front is not standing on */
static void inside_left(short window, short *x, short *y)
{
    intin[0] = window;
    intin[1] = 4;                                    /* WF_WORKXYWH */
    call_aes(104, 6, 5, 0, 0);

    *x = (short)(intout[1] + 2);
    *y = (short)(intout[2] + intout[4] / 2);
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short work_in[11], work_out[57];
    short back, front, told;
    short dx, dy, dw, dh;
    short ox, oy, bx, by, tx, ty, qx = 0, qy = 0;
    short me;
    short pxy[4];
    short i;

    (void)argc; (void)argv;

    me = appl_init();

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;
    v_opnvwk(work_in, &handle, work_out);

    /* Where windows may go, which is the screen below the menu bar */
    intin[0] = 0;
    intin[1] = 4;                                    /* WF_WORKXYWH */
    call_aes(104, 6, 5, 0, 0);
    dx = intout[1]; dy = intout[2]; dw = intout[3]; dh = intout[4];

    /* One over the whole of it, and a smaller one in the middle, opened
     * second so that it is the one in front */
    back = open_window("Back", dx, dy, dw, dh);
    front = open_window("Front", (short)(dx + dw / 4), (short)(dy + dh / 4),
                        (short)(dw / 2), (short)(dh / 2));

    check(back > 0 && front > 0 && back != front, 1, "two windows to overlap");

    if (back <= 0 || front <= 0)
    {
        printf("1..%d\n", n);
        return 1;
    }

    fill_window(back, 1);
    fill_window(front, 2);

    /* A point both of them hold, and one only the back one does */
    inside(front, &ox, &oy);
    inside_left(back, &bx, &by);

    check(pen_at(bx, by), 1, "the back window is drawn in where it alone is");
    check(pen_at(ox, oy), 2,
          "and where they overlap what shows is the window in front");

    /*
     * The back one brought forward, with nothing redrawn.
     *
     * An Atari would have had the front window's pixels there and nothing
     * else: one screen, and the front window drew last. Here each window kept
     * its own picture, so what comes forward is what the back window drew.
     */
    intin[0] = back;
    intin[1] = 10;                                   /* WF_TOP */
    intin[2] = 0; intin[3] = 0; intin[4] = 0; intin[5] = 0;
    call_aes(105, 6, 1, 0, 0);                       /* wind_set */

    check(pen_at(ox, oy), 1,
          "the one behind kept its own picture under the other one");

    /* And back again, which says the front one kept its own as well */
    intin[0] = front;
    intin[1] = 10;                                   /* WF_TOP */
    intin[2] = 0; intin[3] = 0; intin[4] = 0; intin[5] = 0;
    call_aes(105, 6, 1, 0, 0);

    check(pen_at(ox, oy), 2, "and so did the one in front");
    check(pen_at(bx, by), 1, "with neither of them having drawn again");

    /*
     * And a third one, drawn the other way an application draws: from the
     * message.
     *
     * Microsoft Write redraws its clipboard window straight out of the
     * WM_REDRAW it is handed - the rectangle is in the message - without ever
     * asking the window anything. Nothing else in this file reaches that,
     * every other drawing here having asked wind_get first, and it is the only
     * thing that says whose drawing it is when an application works that way.
     */
    told = open_window("Told", (short)(dx + 2), (short)(dy + dh / 2),
                       (short)(dw / 3), (short)(dh / 3 - 2));

    check(told > 0, 1, "a third window over the first");

    for (i = 0; i < 16 && told > 0; i++)
    {
        short happened;
        short message[8];

        intin[0] = 0x0030;                           /* MU_MESAG|MU_TIMER */
        intin[14] = 0; intin[15] = 0;
        addrin[0] = (long)message;
        happened = call_aes(25, 16, 7, 1, 0);        /* evnt_multi */

        if (!(happened & 0x0010))
            break;

        if (message[0] != 20 || message[3] != told)  /* WM_REDRAW */
            continue;

        /* The rectangle the message named, and nothing asked of the window */
        pxy[0] = message[4];
        pxy[1] = message[5];
        pxy[2] = (short)(message[4] + message[6] - 1);
        pxy[3] = (short)(message[5] + message[7] - 1);
        vs_clip(handle, 1, pxy);

        vswr_mode(handle, MD_REPLACE);
        vsf_interior(handle, FIS_SOLID);
        vsf_color(handle, 3);
        v_bar(handle, pxy);

        pxy[0] = 0; pxy[1] = 0; pxy[2] = 0; pxy[3] = 0;
        vs_clip(handle, 0, pxy);

        /*
         * And something drawn straight afterwards, still inside the same
         * answer to the same message, but nowhere near the window the message
         * named. Whatever was said, that cannot be that window's drawing, so
         * it goes where it is instead - which is the window in front there.
         */
        qx = (short)(message[4] + message[6] + 8);
        qy = (short)(message[5] + 4);

        pxy[0] = (short)(qx - 2); pxy[1] = (short)(qy - 2);
        pxy[2] = (short)(qx + 2); pxy[3] = (short)(qy + 2);
        vs_clip(handle, 1, pxy);
        vsf_color(handle, 3);
        v_bar(handle, pxy);

        pxy[0] = 0; pxy[1] = 0; pxy[2] = 0; pxy[3] = 0;
        vs_clip(handle, 0, pxy);
        break;
    }

    inside(told, &tx, &ty);
    check(pen_at(tx, ty), 3, "what it drew from the message is its own");
    check(pen_at(qx, qy), 3,
          "and what it drew outside that window went where it was drawn");

    intin[0] = back;
    intin[1] = 10;                                   /* WF_TOP */
    intin[2] = 0; intin[3] = 0; intin[4] = 0; intin[5] = 0;
    call_aes(105, 6, 1, 0, 0);

    check(pen_at(tx, ty), 1, "and went nowhere near the window underneath");

    intin[0] = told;
    call_aes(102, 1, 1, 0, 0);                       /* wind_close */
    intin[0] = told;
    call_aes(103, 1, 1, 0, 0);                       /* wind_delete */

    intin[0] = front;
    intin[1] = 10;                                   /* WF_TOP */
    intin[2] = 0; intin[3] = 0; intin[4] = 0; intin[5] = 0;
    call_aes(105, 6, 1, 0, 0);

    /*
     * The window behind drawing a piece of itself that the window in front is
     * standing on, with nothing but wind_get to say so.
     *
     * An application updating one field of a document does this: it asks the
     * window where its work area is and draws a few pixels of it. Where those
     * few pixels are under another window, the rectangle is inside both and
     * only the asking says which of them is meant.
     */
    {
        short rx = (short)(ox - 10), ry = (short)(oy - 10);

        intin[0] = back;
        intin[1] = 4;                                /* WF_WORKXYWH */
        call_aes(104, 6, 5, 0, 0);                   /* wind_get */

        pxy[0] = rx; pxy[1] = ry;
        pxy[2] = (short)(rx + 5); pxy[3] = (short)(ry + 5);
        vs_clip(handle, 1, pxy);

        vswr_mode(handle, MD_REPLACE);
        vsf_interior(handle, FIS_SOLID);
        vsf_color(handle, 3);
        v_bar(handle, pxy);

        pxy[0] = 0; pxy[1] = 0; pxy[2] = 0; pxy[3] = 0;
        vs_clip(handle, 0, pxy);

        check(pen_at((short)(rx + 2), (short)(ry + 2)), 2,
              "asking a window behind where it is does not move it forward");

        intin[0] = back;
        intin[1] = 10;                               /* WF_TOP */
        intin[2] = 0; intin[3] = 0; intin[4] = 0; intin[5] = 0;
        call_aes(105, 6, 1, 0, 0);

        check(pen_at((short)(rx + 2), (short)(ry + 2)), 3,
              "and what it drew after asking is that window's");

        intin[0] = front;
        intin[1] = 10;                               /* WF_TOP */
        intin[2] = 0; intin[3] = 0; intin[4] = 0; intin[5] = 0;
        call_aes(105, 6, 1, 0, 0);
    }

    /*
     * And the window behind redrawing a piece of itself that the window in
     * front is standing on.
     *
     * This is the one the message has to answer. Everywhere else the drawing
     * says whose it is by where it is - an application clips to the window it
     * is drawing in, and the window that holds the clipping rectangle is the
     * one - but a rectangle this small is inside both windows, and the one in
     * front holds it too. Only the message names the window behind.
     *
     * The message is one the application sends itself, which is how it can be
     * a small rectangle of a window that nothing has uncovered.
     */
    {
        short message[8];
        short rx = (short)(ox + 4), ry = (short)(oy + 4);

        message[0] = 20;                             /* WM_REDRAW */
        message[1] = me;
        message[2] = 0;
        message[3] = back;
        message[4] = rx; message[5] = ry;
        message[6] = 6; message[7] = 6;

        intin[0] = me;
        intin[1] = 16;
        addrin[0] = (long)message;
        call_aes(12, 2, 1, 1, 0);                    /* appl_write */

        for (i = 0; i < 16; i++)
        {
            short happened;

            intin[0] = 0x0030;                       /* MU_MESAG|MU_TIMER */
            intin[14] = 0; intin[15] = 0;
            addrin[0] = (long)message;
            happened = call_aes(25, 16, 7, 1, 0);    /* evnt_multi */

            if (!(happened & 0x0010))
                break;

            if (message[0] != 20 || message[3] != back)
                continue;

            pxy[0] = message[4];
            pxy[1] = message[5];
            pxy[2] = (short)(message[4] + message[6] - 1);
            pxy[3] = (short)(message[5] + message[7] - 1);
            vs_clip(handle, 1, pxy);

            vswr_mode(handle, MD_REPLACE);
            vsf_interior(handle, FIS_SOLID);
            vsf_color(handle, 3);
            v_bar(handle, pxy);

            pxy[0] = 0; pxy[1] = 0; pxy[2] = 0; pxy[3] = 0;
            vs_clip(handle, 0, pxy);
            break;
        }

        check(pen_at((short)(ox + 6), (short)(oy + 6)), 2,
              "a window behind redrawing under one in front is still behind");

        intin[0] = back;
        intin[1] = 10;                               /* WF_TOP */
        intin[2] = 0; intin[3] = 0; intin[4] = 0; intin[5] = 0;
        call_aes(105, 6, 1, 0, 0);

        check(pen_at((short)(ox + 6), (short)(oy + 6)), 3,
              "and it is the window behind that it landed in");

        intin[0] = front;
        intin[1] = 10;                               /* WF_TOP */
        intin[2] = 0; intin[3] = 0; intin[4] = 0; intin[5] = 0;
        call_aes(105, 6, 1, 0, 0);
    }

    /*
     * And drawing that nothing said anything about at all, which is placed by
     * where it is and nothing else.
     *
     * A rectangle that runs from outside the window in front to inside it is
     * held by the window behind and touched by both. It belongs to the one
     * that holds the whole of it: a program drawing a line across its own
     * window has not stopped drawing in that window because something else is
     * lying across part of it.
     *
     * The wait is what makes it unsaid. Whatever the application was answering
     * ends there, so nothing is left over to place the drawing by.
     */
    {
        short lx = (short)(dx + dw / 16), ly = (short)(oy + 12);
        short rw = (short)(ox - lx + 8);
        short happened;
        short message[8];

        /* Something said about the other window first, so that the wait has
         * something to end: left standing, it would take the drawing below
         * into the window in front instead */
        intin[0] = front;
        intin[1] = 4;                                /* WF_WORKXYWH */
        call_aes(104, 6, 5, 0, 0);                   /* wind_get */

        intin[0] = 0x0020;                           /* MU_TIMER alone */
        intin[14] = 0; intin[15] = 0;
        addrin[0] = (long)message;
        happened = call_aes(25, 16, 7, 1, 0);        /* evnt_multi */
        (void)happened;

        pxy[0] = lx; pxy[1] = ly;
        pxy[2] = (short)(lx + rw - 1); pxy[3] = (short)(ly + 3);
        vs_clip(handle, 1, pxy);

        vswr_mode(handle, MD_REPLACE);
        vsf_interior(handle, FIS_SOLID);
        vsf_color(handle, 3);
        v_bar(handle, pxy);

        pxy[0] = 0; pxy[1] = 0; pxy[2] = 0; pxy[3] = 0;
        vs_clip(handle, 0, pxy);

        check(pen_at((short)(lx + 2), (short)(ly + 1)), 3,
              "unsaid drawing lands in the window that holds the whole of it");
    }

    /*
     * And the one in front closed, which is the same question from the other
     * side: what is under a window is whatever was under it.
     */
    intin[0] = front;
    call_aes(102, 1, 1, 0, 0);                       /* wind_close */

    check(pen_at(ox, oy), 1, "a window closed shows what was underneath it");

    intin[0] = front;
    call_aes(103, 1, 1, 0, 0);                       /* wind_delete */
    intin[0] = back;
    call_aes(102, 1, 1, 0, 0);
    intin[0] = back;
    call_aes(103, 1, 1, 0, 0);

    printf("1..%d\n", n);

    v_clsvwk(handle);
    appl_exit();

    return fails ? 1 : 0;
}
