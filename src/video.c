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

/* See video.h for what this is. */

#include "video.h"

#include <time.h>

#include "gem_p.h"
#include "gfx.h"
#include "memory.h"
#include "settings.h"
#include "shifter.h"
#include "surface.h"
#include "tossystem.h"

/* How many instructions go by between looks at the clock - the same trade
 * interrupt.c's TICK_INSTRUCTIONS makes, and larger, a frame being a much
 * longer time than a timer's tick */
#define TICK_INSTRUCTIONS (65536)

/* A frame, at fifty of them a second */
#define FRAME_NS (20000000LL)

static int taken;
static int windowed;
static struct surface *picture;
static long long last_frame;
static unsigned colours_shown;

static long long now_ns(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* The shape the shifter's mode register says the picture is. The fourth
 * value is not a mode, and an ST showed it as the high one. */
static void shape(int16_t *width, int16_t *height, int16_t *planes)
{
    switch (shifter_rez())
    {
    case 0:
        *width = 320, *height = 200, *planes = 4;
        break;
    case 1:
        *width = 640, *height = 200, *planes = 2;
        break;
    default:
        *width = 640, *height = 400, *planes = 1;
        break;
    }
}

int video_showing(void)
{
    return taken;
}

void video_frame(void)
{
    int16_t width, height, planes;
    const uint8_t *bytes;
    const char *shot;
    int changed;

    if (!taken)
    {
        if (shifter_base() == tos_screen_base())
            return;

        taken = 1;

        /*
         * A window needs the connection to the compositor, and that is opened
         * when GEM starts - which a program that draws for itself may never
         * do. So it is started here, the way the console starts it for a
         * program that came out of GEM, and only when there is a desktop to
         * show anything on.
         */
        if (gfx_possible())
            gem_start();
    }

    last_frame = now_ns();

    shape(&width, &height, &planes);

    /* A different mode is a different shape of window */
    if (picture && (surface_width(picture) != (uint16_t)width
                    || surface_height(picture) != (uint16_t)height
                    || surface_planes(picture) != (uint16_t)planes))
    {
        if (windowed)
            gfx_video_close();
        windowed = 0;

        surface_free(picture);
        picture = 0;
    }

    changed = 0;

    if (!picture)
    {
        picture = surface_create((uint16_t)width, (uint16_t)height,
                                 (uint16_t)planes);
        if (!picture)
            return;

        changed = 1;
    }

    /*
     * The base pointed at something that is not memory, which a program in
     * the middle of setting it up a byte at a time can do. The last picture
     * stays up, which is what the glass would have gone on showing.
     */
    bytes = tos_mem_span(shifter_base(),
                         (uint32_t)width / 16 * planes * 2 * height);
    if (!bytes)
        return;

    changed |= surface_load_atari(picture, bytes);

    /* A colour changing changes the picture without anything in it having
     * been written, which is how a program flashes the screen for a bell */
    if (shifter_colour_changes() != colours_shown)
    {
        colours_shown = shifter_colour_changes();
        changed = 1;
    }

    if (gfx_showing() && !windowed)
    {
        gfx_video_open(picture, width, height);
        windowed = 1;
        changed = 1;
    }

    if (!changed)
        return;

    gfx_present();

    shot = setting("TOSEMU_SCREENSHOT");
    if (shot)
        surface_write_ppm(picture, shot);
}

void video_tick(void)
{
    static int countdown;

    if (--countdown > 0)
        return;

    countdown = TICK_INSTRUCTIONS;

    if (!taken && shifter_base() == tos_screen_base())
        return;

    if (now_ns() - last_frame < FRAME_NS)
        return;

    video_frame();
}
