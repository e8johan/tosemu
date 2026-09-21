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

#include <stdio.h>
#include <string.h>
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

/*
 * How long the base has to be back on the machine's own screen before the
 * picture steps aside for the program's windows, in milliseconds.
 *
 * Long enough that a swap which is over at once never takes it down: a
 * debugger stepping over a trap hands the screen to the program and takes it
 * back again, and a program that flips between two screens every frame is on
 * the machine's own one half the time. Short enough that when the program
 * really has been handed the screen - it is running, or waiting in its own
 * windows - they are what is left without anybody having to wait for it.
 */
#define HANDED_BACK_MS (250)

static int taken;               /* the video hardware has been taken over */
static int showing;             /* and the picture is up */
static int windowed;
static struct surface *picture;
static long long last_frame;
static long long back_since;    /* when the base came home, nought if not */
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

/*
 * Whether the picture stays up when the screen is handed back, which is a
 * setting - see TOSEMU_PICTURE in settings.c. Worked out once, being asked
 * about fifty times a second.
 */
static int keeps(void)
{
    static int decided, keep;
    const char *said;

    if (decided)
        return keep;

    decided = 1;
    said = setting("TOSEMU_PICTURE");

    if (!said || !*said || strcmp(said, "hide") == 0)
        keep = 0;
    else if (strcmp(said, "keep") == 0)
        keep = 1;
    else
    {
        printf("tosemu: picture = %s, which is neither hide nor keep, "
               "so it is hidden\n", said);
        keep = 0;
    }

    return keep;
}

int video_taken(void)
{
    return taken;
}

int video_showing(void)
{
    return showing;
}

void video_forget(void)
{
    if (picture)
        surface_free(picture);

    picture = 0;
    taken = 0;
    showing = 0;
    windowed = 0;
    back_since = 0;
    colours_shown = 0;
}

/*
 * How long the base has been back on the screen the machine was built with,
 * in milliseconds, or -1 while it is anywhere else. That screen is the one GEM
 * and the console stand for, and they are on the desktop already.
 */
static long home_for(long long now)
{
    if (shifter_base() != tos_screen_base())
    {
        back_since = 0;
        return -1;
    }

    if (!back_since)
        back_since = now;

    return (long)((now - back_since) / 1000000LL);
}

static void step_aside(void)
{
    showing = 0;

    if (windowed)
    {
        gfx_video_close();
        gfx_flush();
    }

    windowed = 0;
}

long video_settle(void)
{
    long home;

    if (!showing || keeps())
        return -1;

    home = home_for(now_ns());

    if (home < 0 || !gem_has_windows())
        return -1;

    if (home >= HANDED_BACK_MS)
    {
        video_frame();
        return -1;
    }

    return HANDED_BACK_MS - home;
}

void video_frame(void)
{
    int16_t width, height, planes;
    const uint8_t *bytes;
    const char *shot;
    long long now = now_ns();
    long home;
    int changed = 0;

    if (!taken)
    {
        if (shifter_base() == tos_screen_base())
            return;

        taken = 1;
        showing = 1;

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

    last_frame = now;

    /*
     * Handed back, or taken again. The picture only steps aside when the
     * program has something else of its own up: with nothing, putting it away
     * would leave nothing on the desktop to look at or to type at, and a
     * debugger showing the program's screen waits for a key before it takes
     * its own back.
     */
    home = home_for(now);

    if (home < 0 && !showing)
    {
        showing = 1;
        changed = 1;
    }
    else if (showing && home >= HANDED_BACK_MS && !keeps()
             && gem_has_windows())
        step_aside();

    if (!showing)
        return;

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
