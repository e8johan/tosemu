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
 * The screen memory and GEM's screen being one thing, as they were on an ST.
 *
 * EWeb scrolls a line by moving the rows already in its window up through the
 * screen memory itself and drawing only the new one, and that came out as a
 * window of nothing with one line in it: the memory was a block of its own,
 * which the VDI never drew in and nothing showed.
 *
 * So both ways round, in a window: what the VDI drew is in the memory, what
 * is written in the memory is what the VDI then reads back, and rows moved in
 * the memory are moved on the screen.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>
#include <mint/osbind.h>

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

static short handle, planes;
static long line;
static unsigned char *base;

/* A pixel's colour index, from the planes in the memory */
static int in_memory(short x, short y)
{
    const unsigned short *group = (const unsigned short *)(base + y * line) + (x / 16) * planes;
    int bit = 15 - (x & 15), index = 0, p;

    for (p = 0; p < planes; p++)
        index |= ((group[p] >> bit) & 1) << p;
    return index;
}

/* And as the VDI reads it back */
static int on_screen(short x, short y)
{
    short value, index;

    v_get_pixel(handle, x, y, &value, &index);
    return index;
}

static void bar(short x1, short y1, short x2, short y2, short colour)
{
    short box[4];

    box[0] = x1; box[1] = y1; box[2] = x2; box[3] = y2;
    vsf_interior(handle, 1);
    vsf_color(handle, colour);
    vs_clip(handle, 1, box);
    v_bar(handle, box);
}

int main(void)
{
    short work_in[11], work_out[57], ext[57], wchar, hchar, wbox, hbox, i;
    short window, wx, wy, ww, wh;
    int moved_ok = 1;

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
    vq_extnd(handle, 1, ext);
    planes = ext[4];
    line = (long)(work_out[0] + 1) / 8 * planes;
    base = (unsigned char *)Logbase();

    window = wind_create(0, 0, hbox, 320, 200);
    wind_open(window, 0, hbox, 320, 200);
    wind_get(window, WF_WORKXYWH, &wx, &wy, &ww, &wh);

    /* Two bands of colour 1 and an empty one between, in the window */
    bar(wx, wy, wx + ww - 1, wy + wh - 1, 0);
    bar(wx + 16, wy + 10, wx + 111, wy + 19, 1);
    bar(wx + 16, wy + 30, wx + 111, wy + 39, 1);

    check(in_memory(wx + 40, wy + 15), 1, "what the VDI drew in a window is in the screen memory");
    check(in_memory(wx + 40, wy + 25), 0, "and what it did not draw is not");

    /* A group of sixteen pixels cleared through the memory */
    memset(base + (wy + 15) * line + ((wx + 32) / 16) * planes * 2, 0, planes * 2);
    check(on_screen(wx + 40, wy + 15), 0, "what is written in the memory is what the VDI reads back");
    check(on_screen(wx + 40, wy + 16), 1, "and only that");

    /* The first band moved down twenty rows, a row at a time from the bottom,
     * as EWeb moves its window's rows */
    for (i = 19; i >= 10; i--)
        memcpy(base + (wy + i + 20) * line, base + (wy + i) * line, line);
    for (i = 30; i < 40; i++)
        if (on_screen(wx + 80, wy + i) != 1 || on_screen(wx + 40, wy + i) != (i == 35 ? 0 : 1))
            moved_ok = 0;
    check(moved_ok, 1, "rows moved in the memory are moved on the screen, the cleared group with them");

    /* And the VDI drawing over what was written, after it */
    bar(wx + 16, wy + 30, wx + 111, wy + 39, 0);
    check(in_memory(wx + 80, wy + 35), 0, "the VDI draws over what the memory had");

    wind_close(window);
    wind_delete(window);
    v_clsvwk(handle);
    appl_exit();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
