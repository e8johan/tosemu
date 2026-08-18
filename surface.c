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

int surface_write_ppm(const struct surface *s, const char *path)
{
    FILE *f = fopen(path, "wb");
    uint16_t x, y;

    if (!f)
        return 0;

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
