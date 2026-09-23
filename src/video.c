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
#include "screen.h"
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

/*
 * Whether the video hardware is the program's rather than the machine's.
 *
 * Two ways of taking it over and either will do. Pointing the base at memory
 * of its own is one, and is what a program with a picture to show does. Setting
 * the mode is the other: a program that means to draw on the screen TOS handed
 * it, in a resolution of its own choosing, never moves the base anywhere - and
 * before the mode counted, that program was indistinguishable from one which
 * had done nothing at all.
 *
 * This is one question and not two because everything below turns on it: which
 * shape the picture is, whether there is a picture at all, and whether the
 * screen has been handed back. A program that has taken the hardware over by
 * either route has taken it over for all three.
 *
 * The mode is the one of the two that cannot be given back. Moving the base
 * home again is a program saying it has finished with the hardware; setting the
 * mode back to what the machine came up in is not the same statement, and
 * cannot be read as one - on a TT screen the register already says 2, so a
 * program asking for the ST's high resolution would be indistinguishable from
 * one undoing itself. So a program that has set the mode keeps the picture for
 * the rest of the run. Which is the right way round: it has shown that it draws
 * for itself, and the thing it would be handing back to is a GEM screen it is
 * not using.
 */
static int hardware_taken_over(void)
{
    return shifter_base() != tos_screen_base() || shifter_rez_set();
}

/* The shape the shifter's mode register says the picture is. The fourth
 * value is not a mode, and an ST showed it as the high one. */
static void shape(int16_t *width, int16_t *height, int16_t *planes)
{
    /*
     * Except on the screen the machine was built with, while it is still in
     * the mode the machine came up in. That screen is whatever shape it was
     * asked for and need not be one the register can say - a TT screen is
     * described by its plane count alone, see shifter_init - so the register
     * cannot be what describes it, and the picture showing it is what `always`
     * is for.
     */
    if (!hardware_taken_over())
    {
        static int16_t was_w, was_h, was_p;

        if (!was_w)
            screen_mode(&was_w, &was_h, &was_p);

        *width = was_w, *height = was_h, *planes = was_p;
        return;
    }

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

/* What the picture does, which is a setting - see TOSEMU_PICTURE in
 * settings.c */
enum {
    HIDE,       /* steps aside when the screen is handed back */
    KEEP,       /* stays up once a program has taken the hardware over */
    ALWAYS      /* is up from the start, whoever the screen belongs to */
};

/* Worked out once, being asked about fifty times a second */
static int wanted(void)
{
    static int decided, mode;
    const char *said;

    if (decided)
        return mode;

    decided = 1;
    said = setting("TOSEMU_PICTURE");

    if (!said || !*said || strcmp(said, "hide") == 0)
        mode = HIDE;
    else if (strcmp(said, "keep") == 0)
        mode = KEEP;
    else if (strcmp(said, "always") == 0)
        mode = ALWAYS;
    else
    {
        printf("tosemu: picture = %s, which is none of hide, keep and always, "
               "so it is hidden\n", said);
        mode = HIDE;
    }

    return mode;
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
 * How long the video hardware has been the machine's again, in milliseconds, or
 * -1 while the program still has it. That means the base back on the screen the
 * machine was built with and the mode never having been set - see
 * hardware_taken_over. That screen is the one GEM and the console stand for,
 * and they are on the desktop already.
 */
static long home_for(long long now)
{
    if (hardware_taken_over())
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

    if (!showing || wanted() != HIDE)
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
    uint32_t wants;
    long home;
    int changed = 0;

    if (!taken)
    {
        /*
         * Nothing until a program takes the video hardware over - unless the
         * picture is to be up whatever it has done, which is what `always` is:
         * a program that draws on the screen the machine came with and leaves
         * the mode alone, as every program that is not a GEM one may, is then
         * shown doing it rather than drawing where nobody can see.
         */
        if (!hardware_taken_over() && wanted() != ALWAYS)
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
    else if (showing && home >= HANDED_BACK_MS && wanted() == HIDE
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
    wants = ((uint32_t)width + 15) / 16 * 2 * planes * height;

    bytes = tos_mem_span(shifter_base(), wants);
    if (!bytes)
    {
        /*
         * Unless the mode is what made it too large, which is not a moment in
         * the middle of anything and will not come right on its own. A screen
         * as large as a modern display in a single plane can be smaller than
         * the 32000 bytes every one of the ST's modes reads, so a program that
         * asks for one of them on such a machine points the hardware at more
         * memory than the machine set aside for a screen - and a picture that
         * silently stopped changing is the hardest thing here to diagnose.
         * Said once, this being asked fifty times a second.
         */
        static int said;

        if (shifter_rez_set() && !said)
        {
            said = 1;
            printf("tosemu: resolution %d reads %u bytes and there is not that "
                   "much memory at 0x%x, so it cannot be shown\n",
                   (int)shifter_rez(), (unsigned)wants,
                   (unsigned)shifter_base());
            fflush(stdout);
        }

        return;
    }

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

    if (!taken && !hardware_taken_over() && wanted() != ALWAYS)
        return;

    if (now_ns() - last_frame < FRAME_NS)
        return;

    video_frame();
}
