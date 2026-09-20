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
 * Two windows that overlap, each drawing something the other one is not.
 *
 * On an Atari these two would be one in front of the other on one screen, and
 * the rectangle they share would belong to whichever was in front. Here they
 * are two windows on somebody's desktop, and what the rectangle they share
 * should show is different in each of them: the one in front shows its own
 * picture, and the one behind shows the part of its own picture that the front
 * one is sitting on top of.
 *
 * Which is the whole point of this demo. It is here to be looked at rather than
 * to be run through: the two fill themselves with patterns that cannot be
 * mistaken for one another, so what shows in the overlap says at a glance
 * whose pixels reached which window.
 *
 * It draws the way GEM applications draw, because that is what the emulator has
 * to work from. A redraw is bracketed by wind_update, the visible part of the
 * window is asked for as a list of rectangles, and each rectangle is clipped to
 * before anything is put in it. Nothing in that says which window is being
 * drawn except the handle in the wind_get, which is why it is written out
 * rather than shortened.
 */

#include <gem.h>
#include <stdio.h>

#define W_HAS_NAME    (0x0001)
#define W_HAS_CLOSER  (0x0002)
#define W_HAS_MOVER   (0x0008)

/* gemlib names the wind_get fields and the wind_update ones already */

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

static short handle;

/* The two of them, and what each one calls itself */
static struct {
    short window;
    const char *name;
    short x, y, w, h;

    /* What it fills itself with: the pattern index, and a word to write */
    short pattern;
    const char *says;
} panes[2] = {
    { 0, "Underneath", 20,  40, 400, 260, 4, "UNDER" },
    { 0, "On top",    200, 150, 300, 200, 7, "OVER"  }
};

/*
 * One rectangle of one window, filled and labelled.
 *
 * The clipping is what makes this a redraw rather than a drawing: everything
 * below is asked for over the whole work area and only the part inside the
 * rectangle lands, which on an Atari is how a window behind another one drew
 * without drawing on it.
 */
static void draw_part(int which, short cx, short cy, short cw, short ch)
{
    short pxy[4];
    short x, y, wide, high;
    short i;

    intin[0] = panes[which].window;
    intin[1] = WF_WORKXYWH;
    call_aes(104, 6, 5, 0, 0);
    x = intout[1]; y = intout[2]; wide = intout[3]; high = intout[4];

    pxy[0] = cx; pxy[1] = cy;
    pxy[2] = cx + cw - 1; pxy[3] = cy + ch - 1;
    vs_clip(handle, 1, pxy);

    vswr_mode(handle, MD_REPLACE);

    /* The pattern, which is the part that says whose pixels these are */
    vsf_interior(handle, FIS_PATTERN);
    vsf_style(handle, panes[which].pattern);
    vsf_color(handle, 1);
    pxy[0] = x; pxy[1] = y;
    pxy[2] = x + wide - 1; pxy[3] = y + high - 1;
    v_bar(handle, pxy);

    /* And the name of it, several times over, so that a corner of the window
     * is enough to tell which one it is. Written in whichever colour the
     * pattern behind it is not. */
    vst_color(handle, which ? 0 : 1);
    vswr_mode(handle, MD_TRANS);
    for (i = 0; i < high; i += 32)
        v_gtext(handle, x + 12, y + 20 + i, (char *)panes[which].says);

    vs_clip(handle, 0, pxy);
}

/* The whole of one window, a rectangle at a time, the way GEM asks for it */
static void draw(int which)
{
    if (panes[which].window < 0)
        return;

    intin[0] = BEG_UPDATE;
    call_aes(107, 1, 1, 0, 0);                             /* wind_update */

    intin[0] = panes[which].window;
    intin[1] = WF_FIRSTXYWH;
    call_aes(104, 6, 5, 0, 0);                             /* wind_get */

    while (intout[3] > 0 && intout[4] > 0)
    {
        draw_part(which, intout[1], intout[2], intout[3], intout[4]);

        intin[0] = panes[which].window;
        intin[1] = WF_NEXTXYWH;
        call_aes(104, 6, 5, 0, 0);
    }

    intin[0] = END_UPDATE;
    call_aes(107, 1, 1, 0, 0);
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    short work_in[11], work_out[57];
    short message[8];
    short i, running, open;

    (void)argc; (void)argv;

    appl_init();

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;
    v_opnvwk(work_in, &handle, work_out);

    /*
     * The one underneath first, so that the one on top is the newer of the two
     * and sits in front of it the way GEM would have put it
     */
    for (i = 0; i < 2; i++)
    {
        intin[0] = W_HAS_NAME|W_HAS_CLOSER|W_HAS_MOVER;
        intin[1] = panes[i].x; intin[2] = panes[i].y;
        intin[3] = panes[i].w; intin[4] = panes[i].h;
        panes[i].window = call_aes(100, 5, 1, 0, 0);       /* wind_create */

        intin[0] = panes[i].window;
        intin[1] = 2;                                      /* WF_NAME */
        intin[2] = (short)(((long)panes[i].name) >> 16);
        intin[3] = (short)(((long)panes[i].name) & 0xffff);
        call_aes(105, 6, 1, 0, 0);                         /* wind_set */

        intin[0] = panes[i].window;
        intin[1] = panes[i].x; intin[2] = panes[i].y;
        intin[3] = panes[i].w; intin[4] = panes[i].h;
        call_aes(101, 5, 1, 0, 0);                         /* wind_open */
    }

    draw(0);
    draw(1);

    for (running = 1, open = 2; running; )
    {
        for (i = 0; i < 16; i++)
            intin[i] = 0;
        intin[0] = 0x0010;                                 /* MU_MESAG */
        addrin[0] = (long)message;
        call_aes(25, 16, 7, 1, 0);                         /* evnt_multi */

        switch (message[0])
        {
        case WM_REDRAW:
            for (i = 0; i < 2; i++)
                if (message[3] == panes[i].window)
                    draw(i);
            break;

        case WM_CLOSED:
            for (i = 0; i < 2; i++)
                if (message[3] == panes[i].window)
                {
                    intin[0] = panes[i].window;
                    call_aes(102, 1, 1, 0, 0);             /* wind_close */
                    intin[0] = panes[i].window;
                    call_aes(103, 1, 1, 0, 0);             /* wind_delete */
                    panes[i].window = -1;
                    open--;
                }

            if (open <= 0)
                running = 0;
            break;
        }
    }

    v_clsvwk(handle);
    appl_exit();

    printf("closed\n");

    return 0;
}
