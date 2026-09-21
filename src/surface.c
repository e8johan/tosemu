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

#include "surface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emuvdi/emuvdi.h"

struct surface {
    uint16_t width;
    uint16_t height;
    uint16_t planes;

    /* Words in a row, all planes together. A row is whole words, so a width
     * that is not a multiple of sixteen rounds up and the last few pixels of
     * each row are there but never drawn on. */
    uint16_t words_per_line;

    uint16_t *data;

    /* One rectangle round everything drawn since the damage was last taken,
     * empty when dw is nought - see surface_damage */
    int16_t dx, dy, dw, dh;
};

static struct surface *selected;

struct surface *surface_create(uint16_t width, uint16_t height,
                               uint16_t planes)
{
    struct surface *s;

    /* The VDI addresses planes by shifting rather than multiplying, so it can
     * only do the plane counts an Atari had */
    if (planes != 1 && planes != 2 && planes != 4 && planes != 8)
    {
        printf("Surface: %d planes is not a number an Atari bitmap has\n",
               planes);
        return 0;
    }

    if (width == 0 || height == 0)
        return 0;

    s = calloc(1, sizeof *s);
    if (!s)
        return 0;

    s->width = width;
    s->height = height;
    s->planes = planes;
    s->words_per_line = ((width + 15) / 16) * planes;

    s->data = calloc((size_t)s->words_per_line * height, sizeof *s->data);
    if (!s->data)
    {
        free(s);
        return 0;
    }

    return s;
}

void surface_free(struct surface *s)
{
    if (!s)
        return;

    if (selected == s)
        selected = 0;

    free(s->data);
    free(s);
}

void surface_select(struct surface *s)
{
    selected = s;

    if (s)
        emuvdi_surface_select(s->data, s->width, s->height, s->planes);
}

struct surface *surface_selected()
{
    return selected;
}

uint16_t surface_width(const struct surface *s)
{
    return s->width;
}

uint16_t surface_height(const struct surface *s)
{
    return s->height;
}

uint16_t surface_planes(const struct surface *s)
{
    return s->planes;
}

void surface_copy(struct surface *dst, const struct surface *src)
{
    size_t words;

    if (dst->width != src->width || dst->height != src->height
        || dst->planes != src->planes)
        return;

    words = (size_t)dst->words_per_line * dst->height;

    memcpy(dst->data, src->data, words * sizeof *dst->data);

    /* All of it, which is what was just written over */
    surface_damage(dst, 0, 0, dst->width, dst->height);
}

void surface_copy_rect(struct surface *dst, const struct surface *src,
                       int x, int y, int w, int h)
{
    int x2, y2, row, first, last, group, p;

    if (!dst || !src || dst->width != src->width
        || dst->height != src->height || dst->planes != src->planes)
        return;

    /* Clamped to the surface, like damage is */
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }

    x2 = x + w;
    y2 = y + h;

    if (x2 > dst->width)
        x2 = dst->width;
    if (y2 > dst->height)
        y2 = dst->height;

    if (x >= x2 || y >= y2)
        return;

    /* The groups of plane words the rectangle touches, the first and last of
     * them only in part */
    first = x / 16;
    last = (x2 - 1) / 16;

    for (row = y; row < y2; row++)
    {
        size_t at = (size_t)row * dst->words_per_line;

        for (group = first; group <= last; group++)
        {
            /* Which of the sixteen pixels in this group are inside. The
             * leftmost pixel is the top bit of a word. */
            int from = (group == first) ? x % 16 : 0;
            int to = (group == last) ? (x2 - 1) % 16 : 15;
            uint16_t mask = (uint16_t)((0xffffu >> from)
                                       & (0xffffu << (15 - to)));
            size_t word = at + (size_t)group * dst->planes;

            for (p = 0; p < dst->planes; p++)
                dst->data[word + p] =
                    (uint16_t)((dst->data[word + p] & ~mask)
                               | (src->data[word + p] & mask));
        }
    }

    surface_damage(dst, x, y, x2 - x, y2 - y);
}

void surface_damage(struct surface *s, int x, int y, int w, int h)
{
    int x2, y2;

    if (!s || w <= 0 || h <= 0)
        return;

    /*
     * Clamped to the surface first. A clipping rectangle can be larger than
     * what it clips, and something that cannot say where it drew says the
     * whole of everything and means this.
     */
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }

    x2 = x + w;
    y2 = y + h;

    if (x2 > s->width)
        x2 = s->width;
    if (y2 > s->height)
        y2 = s->height;

    if (x >= x2 || y >= y2)
        return;

    if (s->dw == 0)
    {
        s->dx = (int16_t)x;
        s->dy = (int16_t)y;
        s->dw = (int16_t)(x2 - x);
        s->dh = (int16_t)(y2 - y);

        return;
    }

    /* One rectangle round both. Two changes at opposite corners of a screen
     * come out as the screen, which is the price of keeping one rectangle
     * rather than a list of them - see TODO. */
    if (x < s->dx)
    {
        s->dw = (int16_t)(s->dw + (s->dx - x));
        s->dx = (int16_t)x;
    }
    if (y < s->dy)
    {
        s->dh = (int16_t)(s->dh + (s->dy - y));
        s->dy = (int16_t)y;
    }
    if (x2 > s->dx + s->dw)
        s->dw = (int16_t)(x2 - s->dx);
    if (y2 > s->dy + s->dh)
        s->dh = (int16_t)(y2 - s->dy);
}

int surface_damage_take(struct surface *s, int16_t *x, int16_t *y,
                        int16_t *w, int16_t *h)
{
    if (!s || s->dw == 0)
        return 0;

    *x = s->dx;
    *y = s->dy;
    *w = s->dw;
    *h = s->dh;

    s->dw = 0;
    s->dh = 0;

    return 1;
}

/*
 * And the same said from inside the VDI, about whichever surface it is drawing
 * on. See emuvdi_call, which is the one door every drawing operation goes
 * through and the only place that knows what a call was allowed to touch.
 */
void host_surface_damaged(int16_t x, int16_t y, int16_t w, int16_t h)
{
    surface_damage(selected, x, y, w, h);
}

/*
 * Compared as it is copied, so that only the rows that are different are said
 * to be: a picture is brought across fifty times a second, and most of the
 * time none of it has changed, or one line of text has.
 */
int surface_load_atari(struct surface *s, const uint8_t *bytes)
{
    int first = -1, last = -1;
    int x, y;

    if (!s || !bytes)
        return 0;

    for (y = 0; y < s->height; y++)
    {
        uint16_t *row = s->data + (size_t)y * s->words_per_line;
        const uint8_t *from = bytes + (size_t)y * s->words_per_line * 2;
        int changed = 0;

        for (x = 0; x < s->words_per_line; x++)
        {
            uint16_t word = (uint16_t)((from[2 * x] << 8) | from[2 * x + 1]);

            if (row[x] != word)
            {
                row[x] = word;
                changed = 1;
            }
        }

        if (changed)
        {
            if (first < 0)
                first = y;
            last = y;
        }
    }

    if (first < 0)
        return 0;

    surface_damage(s, 0, first, s->width, last - first + 1);

    return 1;
}

int surface_write_ppm(const struct surface *s, const char *path)
{
    /*
     * Written beside the file and renamed onto it, rather than into it.
     *
     * A screenshot is taken every time the application waits, so the file is
     * being written over and over while somebody is looking at it, and a
     * rename is the only way the looking never lands in the middle of one.
     * Without it a picture read at the wrong moment is however much of it had
     * been written - which looks like the emulator drawing half a screen.
     */
    char *temp = malloc(strlen(path) + 3);
    FILE *f;
    uint16_t x, y;

    if (!temp)
        return 0;

    strcpy(temp, path);
    strcat(temp, ".t");

    f = fopen(temp, "wb");
    if (!f)
    {
        free(temp);
        return 0;
    }

    fprintf(f, "P6\n%d %d\n255\n", s->width, s->height);

    for (y = 0; y < s->height; y++)
    {
        for (x = 0; x < s->width; x++)
        {
            uint32_t argb = emuvdi_palette_argb(surface_pixel(s, x, y));
            unsigned char rgb[3];

            rgb[0] = (argb >> 16) & 0xff;
            rgb[1] = (argb >> 8) & 0xff;
            rgb[2] = argb & 0xff;

            fwrite(rgb, 1, 3, f);
        }
    }

    fclose(f);

    if (rename(temp, path) != 0)
    {
        remove(temp);
        free(temp);
        return 0;
    }

    free(temp);

    return 1;
}

uint16_t surface_pixel(const struct surface *s, uint16_t x, uint16_t y)
{
    const uint16_t *word;
    uint16_t mask;
    uint16_t value = 0;
    int plane;

    if (x >= s->width || y >= s->height)
        return 0;

    word = s->data + (size_t)y * s->words_per_line + (x / 16) * s->planes;
    mask = 0x8000u >> (x & 15);

    for (plane = 0; plane < s->planes; plane++)
        if (word[plane] & mask)
            value |= 1u << plane;

    return value;
}

/*
 * A byte of one plane, spread over eight bytes with a bit in each.
 *
 * This is the whole of how a run is taken faster than a pixel at a time, and
 * it is worth being plain about what it holds. Entry b has, in its byte i, the
 * bit that is 0x80 >> i of b - so the leftmost of the eight pixels a byte of a
 * plane describes ends up in the lowest byte of the entry, the next one along
 * in the next byte, and so on.
 *
 * What that buys is that a plane's whole contribution to eight pixels is one
 * look-up and one shift: the bit a plane puts into a pen is 1 << plane, and
 * shifting the entry by the plane number puts it there in all eight bytes at
 * once. Four planes are then four look-ups, three shifts and three ors, and
 * out of that come eight finished pens - against sixteen tests and sixteen
 * shifts doing it a pixel at a time.
 *
 * The bytes are taken back out by shifting rather than by copying the memory,
 * so nothing here depends on which end of a word this machine puts first.
 */
static uint64_t spread[256];
static int spread_ready;

static void spread_make(void)
{
    int b, i;

    for (b = 0; b < 256; b++)
    {
        uint64_t entry = 0;

        for (i = 0; i < 8; i++)
            if (b & (0x80 >> i))
                entry |= (uint64_t)1 << (8 * i);

        spread[b] = entry;
    }

    spread_ready = 1;
}

void surface_row(const struct surface *s, uint16_t x, uint16_t y,
                 uint16_t count, uint8_t *into)
{
    const uint16_t *word;
    int planes = s->planes;
    uint16_t have = 0;
    uint16_t done = 0;

    /* What of the run is actually on the surface. The rest reads as nought,
     * which is surface_pixel's answer for it and is also what stops this
     * walking off the end of a row. */
    if (y < s->height && x < s->width)
        have = (uint16_t)(s->width - x);

    if (have > count)
        have = count;

    if (have < count)
        memset(into + have, 0, (size_t)(count - have));

    if (!have)
        return;

    if (!spread_ready)
        spread_make();

    word = s->data + (size_t)y * s->words_per_line + (x / 16) * planes;

    while (done < have)
    {
        /* Where in this group of plane words the next pixel is. A group is
         * one word of every plane and holds sixteen pixels, the leftmost of
         * them at the top of each word. */
        int bit = (x + done) & 15;
        int p;

        if ((bit & 7) == 0 && have - done >= 8)
        {
            /*
             * Eight of them at once, out of one byte of each plane - the top
             * byte of the group's words for the first eight and the bottom
             * byte for the second, which is what the shift below picks.
             */
            uint64_t eight = 0;
            int i;

            for (p = 0; p < planes; p++)
                eight |= spread[(word[p] >> (8 - bit)) & 0xff] << p;

            for (i = 0; i < 8; i++)
                into[done + i] = (uint8_t)(eight >> (8 * i));

            done = (uint16_t)(done + 8);
        }
        else
        {
            /* And one at a time for a run that does not begin on a byte of
             * the planes, and for whatever is left at the end of one */
            uint16_t mask = (uint16_t)(0x8000u >> bit);
            unsigned value = 0;

            for (p = 0; p < planes; p++)
                if (word[p] & mask)
                    value |= 1u << p;

            into[done] = (uint8_t)value;

            done++;
        }

        /* Past the end of the group is the start of the next one */
        if (((x + done) & 15) == 0)
            word += planes;
    }
}
