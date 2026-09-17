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
 * Reading pixels back out of the planes.
 *
 * Built for the host rather than for the emulated machine, and for the same
 * reason bin/screentest is: what this checks is not something an application
 * can reach. An application draws through the VDI and sees what it drew; the
 * gathering of a pixel out of the planes happens on the way to a compositor,
 * where a test cannot follow it.
 *
 * surface_pixel is the plain statement of what a pixel is, and surface_row is
 * the same answer arrived at sideways - sixteen pixels out of one group of
 * plane words, eight of them at a time through a table. So the plain one is
 * the authority here and the quick one is checked against it, everywhere,
 * which is the only way a table of spread bits is worth having.
 *
 * Everywhere matters more than usual. The quick way has a middle that does
 * eight at a time and two ends that do not, and the ends are reached by where
 * a run starts and how long it is rather than by anything in the picture - so
 * a window showing the screen from an odd column takes a different path
 * through it than one showing it from an even one, and only one of those would
 * be tried by a test that checked a picture.
 */

#include "surface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/*
 * What surface.c asks emuvdi for.
 *
 * The select is how the VDI is told where to draw, and here it is also the
 * only door to the plane memory: nothing writes a pixel from outside the VDI,
 * and a surface of noughts would check the one value that every way of getting
 * it wrong still answers correctly.
 */
static uint16_t *planes;
static size_t words;

void emuvdi_surface_select(void *data, int width, int height, int count)
{
    planes = data;
    words = (size_t)(((width + 15) / 16) * count) * height;
}

/* The colours are nothing to do with this and nobody here asks for one */
uint32_t emuvdi_palette_argb(int pen)
{
    return 0xff000000u | (unsigned)pen;
}

/* Something in every bit, the same every time it is run */
static void fill(void)
{
    unsigned seed = 20260917u;
    size_t i;

    for (i = 0; i < words; i++)
    {
        seed = seed * 1103515245u + 12345u;
        planes[i] = (uint16_t)(seed >> 16);
    }
}

/*
 * Every start, every length, against surface_pixel - and past both edges,
 * where the answer is nought and the reason to ask is that a run walking off
 * the end of a row would otherwise read the start of the next one.
 */
static void agrees(int width, int height, int count, const char *name)
{
    struct surface *s = surface_create((uint16_t)width, (uint16_t)height,
                                       (uint16_t)count);
    uint8_t got[48];
    int wrong = 0, over = 0;
    int x, y, run, i;

    if (!s)
    {
        check(0, 1, name);
        return;
    }

    surface_select(s);
    fill();

    for (y = 0; y < height + 1; y++)
        for (x = 0; x < width + 18; x++)
            for (run = 0; run < (int)sizeof got; run++)
            {
                memset(got, 0xaa, sizeof got);
                surface_row(s, (uint16_t)x, (uint16_t)y, (uint16_t)run, got);

                for (i = 0; i < run; i++)
                {
                    int want = (x + i < width && y < height)
                             ? surface_pixel(s, (uint16_t)(x + i), (uint16_t)y)
                             : 0;

                    if (got[i] != want)
                        wrong++;
                }

                /* And that it stopped where it was told to */
                if (got[run] != 0xaa)
                    over++;
            }

    surface_free(s);

    check(wrong, 0, name);

    n++;
    if (over == 0)
        printf("ok %d - and wrote no further than it was asked to\n", n);
    else
    {
        fails++;
        printf("not ok %d - and wrote no further than it was asked to "
               "(%d runs did)\n", n, over);
    }
}

/*
 * Where something says it drew, and what comes back out.
 *
 * Saying too much is always allowed and saying too little never is, so what
 * these ask about most is the edges: a rectangle that hangs off the surface,
 * one that is the wrong way round, and the whole-of-everything one that
 * anything unable to say where it drew passes.
 */
/*
 * Takes the damage and says whether it was the rectangle expected. The take
 * itself is part of the answer: without that, a surface that reported nothing
 * would leave the four numbers as whatever the check before it read, and a
 * check comparing them would pass on somebody else's answer.
 */
static int took(struct surface *s, int x, int y, int w, int h)
{
    int16_t gx = -1, gy = -1, gw = -1, gh = -1;

    if (!surface_damage_take(s, &gx, &gy, &gw, &gh))
        return 0;

    return gx == x && gy == y && gw == w && gh == h;
}

static void damage(void)
{
    struct surface *s = surface_create(100, 50, 4);
    int16_t x, y, w, h;

    if (!s)
    {
        check(0, 1, "a surface to damage");
        return;
    }

    check(surface_damage_take(s, &x, &y, &w, &h), 0,
          "a surface nothing has drawn in owes nothing");

    surface_damage(s, 10, 20, 5, 6);
    check(took(s, 10, 20, 5, 6), 1,
          "and one that has owes the rectangle it was told about");

    check(surface_damage_take(s, &x, &y, &w, &h), 0,
          "and taking it is what forgets it");

    /* Two of them, which come back as one rectangle round both */
    surface_damage(s, 10, 10, 2, 2);
    surface_damage(s, 40, 30, 5, 5);
    check(took(s, 10, 10, 35, 25), 1,
          "two places drawn in come back as one rectangle round both");

    /* And the same two the other way about, which is the half of growing a
     * rectangle that moves the corner it is measured from */
    surface_damage(s, 40, 30, 5, 5);
    surface_damage(s, 10, 10, 2, 2);
    check(took(s, 10, 10, 35, 25), 1,
          "and the same however they were drawn in");

    /* And one inside the other, which changes nothing */
    surface_damage(s, 10, 10, 30, 30);
    surface_damage(s, 15, 15, 2, 2);
    check(took(s, 10, 10, 30, 30), 1,
          "and one inside another is the one outside it");

    /* Hanging off the right and the bottom */
    surface_damage(s, 90, 45, 1000, 1000);
    check(took(s, 90, 45, 10, 5), 1,
          "a rectangle running off the end stops at the end");

    /* And off the left and the top, which is where a clipping rectangle with
     * a negative corner would put it */
    surface_damage(s, -20, -30, 25, 35);
    check(took(s, 0, 0, 5, 5), 1,
          "and one starting before the beginning starts at it");

    /* The whole of everything, which is what something that cannot say where
     * it drew passes - see host_surface_damaged */
    surface_damage(s, 0, 0, 32767, 32767);
    check(took(s, 0, 0, 100, 50), 1,
          "a rectangle larger than the surface is the surface");

    /* Nothing at all, said in the two ways it can be */
    surface_damage(s, 10, 10, 0, 5);
    surface_damage(s, 10, 10, 5, -1);
    check(surface_damage_take(s, &x, &y, &w, &h), 0,
          "a rectangle of no size is nothing drawn");

    /* And entirely off the surface, which is a clipping rectangle for a
     * window that has been moved away */
    surface_damage(s, 200, 200, 10, 10);
    check(surface_damage_take(s, &x, &y, &w, &h), 0,
          "and so is one that misses the surface altogether");

    /* Copying one surface over another is all of it drawn in */
    {
        struct surface *from = surface_create(100, 50, 4);

        if (from)
        {
            surface_copy(s, from);
            check(took(s, 0, 0, 100, 50), 1,
                  "and a surface copied over is the whole of one");

            surface_free(from);
        }
    }

    surface_free(s);
}

int main(void)
{
    struct surface *s;
    uint8_t got[32];

    /*
     * One of each plane count an Atari had. Two of the widths are the ones
     * worth being careful about: fifteen is narrower than the word a row is
     * kept in, so most of that word is pixels that are not there, and
     * seventeen leaves one pixel of a second word.
     */
    agrees(15, 3, 1, "a run of a one-plane surface reads as its pixels do");
    agrees(16, 3, 2, "and of a two-plane one");
    agrees(17, 3, 4, "and of a four-plane one");
    agrees(33, 3, 8, "and of an eight-plane one");
    agrees(80, 2, 4, "and of one several words across");
    agrees(641, 2, 4, "and of one the width a window really is");

    /*
     * The pens themselves, said outright rather than by agreement. Everything
     * above would still pass if both ways of reading a pixel had the planes in
     * the same wrong order, and a screen read that way is one with its colours
     * shuffled.
     */
    s = surface_create(16, 1, 4);
    if (!s)
        return 1;

    surface_select(s);

    /* The lowest plane holds the lowest bit of a pen, and the leftmost pixel
     * is the top bit of a word */
    planes[0] = 0x8000;
    planes[1] = 0x4000;
    planes[2] = 0x2000;
    planes[3] = 0x1000;

    memset(got, 0, sizeof got);
    surface_row(s, 0, 0, 5, got);

    check(got[0], 1, "the first plane is the first bit of a pen");
    check(got[1], 2, "the second the second");
    check(got[2], 4, "the third the third");
    check(got[3], 8, "the fourth the fourth");
    check(got[4], 0, "and a pixel no plane has a bit in is nought");

    /* All of them together, which is the highest pen four planes can hold */
    planes[0] = planes[1] = planes[2] = planes[3] = 0x0100;

    surface_row(s, 0, 0, 16, got);
    check(got[7], 15, "a pixel every plane has a bit in is the last pen");
    check(got[6], 0, "and its neighbours are not");
    check(got[8], 0, "on either side");

    surface_free(s);

    damage();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
