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
 * The picture that goes on the windows.
 *
 * Host built for the same reason bin/screentest is: what happens to this
 * picture afterwards is a compositor drawing it in a task bar, and a task bar
 * is not something a test can arrange. What can be checked is what is handed
 * over, and that is the whole of what could quietly go wrong here.
 *
 * Two things could. The picture is taken out of EmuTOS's icon resource by
 * number, and the numbers belong to somebody else's tree - a resource with its
 * icons in another order would put a folder or a floppy disk on every window
 * and nothing would say so. And a resource is bits and masks rather than
 * pixels, so a picture read out of one by the wrong end of a byte is a picture
 * that is still the right size and still looks like an icon.
 *
 * So this asks whether the picture is the generic application icon: a window
 * with a title bar, which is what EmuTOS draws for a program and what none of
 * the icons either side of it in the file looks like.
 */

#include <stdio.h>

/* The picture, as the build made it - see the rule in the Makefile */
#include "rsc/window-icon.h"

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

/* The same, for a pixel: a number would be the character's code, and "got 32,
 * want 46" says nothing to anyone reading it out of a build log */
static void check_pixel(int got, int want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got %c, want %c)\n", n, name, got, want);
    }
}

/*
 * A pixel, as one of the three things a pixel of a GEM icon can be.
 *
 * There are only three. Where the icon's data says so it is ink, where its
 * mask says so and its data does not it is paper, and where neither does it is
 * the desktop showing through - which has to be no colour at all rather than a
 * transparent black, because the wire wants a premultiplied pixel and a colour
 * behind a zero alpha is not one.
 */
#define INK     ('#')
#define PAPER   ('.')
#define THROUGH (' ')
#define WRONG   ('?')

static int at(int x, int y)
{
    const unsigned char *p = window_icon_argb
                             + ((size_t)y * WINDOW_ICON_SIZE + x) * 4;

    if (p[0] == 0xff && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x00)
        return INK;
    if (p[0] == 0xff && p[1] == 0xff && p[2] == 0xff && p[3] == 0xff)
        return PAPER;
    if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x00)
        return THROUGH;

    return WRONG;
}

/* Whether a row is the same thing from one column to another */
static int run(int y, int from, int to, int want)
{
    int x;

    for (x = from; x <= to; x++)
        if (at(x, y) != want)
            return 0;

    return 1;
}

/* And whether a row has any ink in it at all, which is what tells a title bar
 * from the empty middle of a window */
static int inked(int y)
{
    int x;

    for (x = 0; x < WINDOW_ICON_SIZE; x++)
        if (at(x, y) == INK)
            return 1;

    return 0;
}

static void picture(void)
{
    int size = WINDOW_ICON_SIZE;
    int y, colours = 0;

    /* The size is the protocol's business as much as the picture's: an icon
     * buffer has to be square, and every size offered is a whole number of
     * copies of this one */
    check(size, 32, "the icon is the size an ST icon was drawn at");

    for (y = 0; y < size; y++)
    {
        int x;

        for (x = 0; x < size; x++)
            if (at(x, y) == WRONG)
                colours++;
    }
    check(colours, 0, "and is drawn in ink, paper and nothing else");

    /* A corner of a GEM icon is outside its mask, which is what makes it a
     * shape on the desktop rather than a square */
    check_pixel(at(0, 0), THROUGH,
                "its corners are the desktop showing through");
    check_pixel(at(size - 1, size - 1), THROUGH, "at the far end as well");

    /*
     * And what is between them is a window drawn as tall as the picture. That
     * is already most of the answer: the icons either side of this one in the
     * file are a folder and a document, and both of them start several rows
     * down - a folder to leave room for its tab and a document for its turned
     * corner - so their top row is nothing at all where this one is drawn.
     */
    check_pixel(at(2, 0), PAPER,
                "the picture is drawn to the top of the square");
    check(run(1, 2, size - 3, INK), 1, "where the top of the window is");
    check(run(size - 2, 2, size - 3, INK), 1, "and to the bottom of it");

    check_pixel(at(1, size / 2), INK, "the left side of the window is drawn");
    check_pixel(at(size - 2, size / 2), INK, "and the right side");
    check(run(size / 2, 2, size - 3, PAPER), 1, "with paper between them");

    /*
     * And the title bar across the top of it, which is the rest of the answer:
     * a line ruled the whole width of the window five rows down, something
     * drawn above it and nothing below. Neither of the icons either side has
     * anything of the sort.
     */
    check(inked(3), 1, "there is a title bar under the top");
    check(run(5, 1, size - 2, INK), 1, "with a line ruled under it");
    check(run(7, 2, size - 3, PAPER), 1, "and the empty window below that");
}

int main(void)
{
    picture();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
