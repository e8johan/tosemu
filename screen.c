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
 * Which screen the machine has, which is the one thing about it an application
 * cannot be told twice.
 *
 * This is not a preference. A GEM application is laid out in characters and
 * assumes how many of them fit across, because the resource editor it was
 * drawn in had a screen in mind: a dialog forty-five characters wide is an
 * ordinary dialog on a screen eighty characters across and does not fit at all
 * on one that is forty, where the AES centres it at a negative coordinate and
 * it hangs off both edges.
 *
 * Both the emulator and the daemon read this, and both link it, so that a
 * session with no daemon in it gets the same machine as a session with one.
 */

#include "screen.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

/* How much larger than an ST pixel one on the desktop is. Three is a
 * reasonable guess at a modern display - a 640x400 screen becomes 1920x1200. */
#define SCALE_DEFAULT (3)
#define SCALE_MAX     (16)

int screen_scale(void)
{
    /* Said once rather than once a window, which is how often this is asked */
    static int complained;
    const char *said = setting("TOSEMU_SCALE");
    int n;

    if (!said)
        return SCALE_DEFAULT;

    n = atoi(said);
    if (n >= 1 && n <= SCALE_MAX)
        return n;

    if (!complained)
    {
        complained = 1;
        fprintf(stderr, "TOSEMU_SCALE has to be a whole number between 1 and "
                        "%d, so %s is ignored\n", SCALE_MAX, said);
    }

    return SCALE_DEFAULT;
}

/*
 * The screens that are a size rather than a rule.
 *
 * The ones the machines had, named the way they named them. The ST's high
 * resolution screen is the default, being the one GEM applications were
 * written for. Its low resolution one is for the things that need colours to
 * be worth testing - the AES draws in sixteen of them and a monochrome screen
 * has two - and its medium one is here because the machine had it. The TT's
 * two are here because they cost nothing: the same planes in another shape,
 * and everything that lays itself out in characters simply has more of them to
 * work with.
 *
 * The TT's third is not here, and neither are the Falcon's. Both want
 * something the VDI was not built with rather than another line in this table.
 * The TT's low resolution screen is eight planes, which the VDI draws in
 * happily, but its palette is sixteen entries unless EXTENDED_PALETTE is on -
 * init_colors walks MAP_COL and REV_MAP_COL to numcolors either way, so
 * asking for it runs off the end of both and a cleared screen reads back as
 * 254. EXTENDED_PALETTE is (CONF_WITH_VIDEL || CONF_WITH_TT_SHIFTER), which
 * means building the VDI with support for hardware that is not there. The
 * Falcon's is sixteen bits to a pixel rather than planes, and surface.h says
 * why that is not a small change: planes interleaved a word at a time is the
 * shape that lets the VDI be EmuTOS's code rather than a rewrite of it.
 */
static const struct {
    const char *name;
    int16_t width, height, planes;
} modes[] = {
    { "low",        320, 200, 4 },
    { "medium",     640, 200, 2 },
    { "high",       640, 400, 1 },
    { "tt-medium",  640, 480, 4 },
    { "tt-high",   1280, 960, 1 },
};

#define MODE_DEFAULT (2)    /* high */

/*
 * And the two that are a rule: as large as the display will hold, in colour or
 * in black and white.
 *
 * They are not Atari screens and are not pretending to be. What they are for
 * is a GEM application having the room a modern display has, which is the one
 * thing the machine could not give it - the rest of GEM does not mind, because
 * a resource is measured in characters and more of them across is simply more
 * room.
 */
static const struct {
    const char *name;
    int16_t planes;
} native[] = {
    { "native-mono",  1 },
    { "native-color", 4 },
};

void screen_from_display(int32_t pixels_w, int32_t pixels_h, int32_t out_scale,
                         int16_t *width, int16_t *height)
{
    int32_t w, h;

    /*
     * Two divisions, and both of them are real.
     *
     * The first is the compositor's own, which turns the pixels a display has
     * into the pixels a window is measured in. gfx.c never sets a buffer
     * scale, so what it hands over is in the second of those: on a display
     * that reports twice the pixels and a scale of two, a window asking for a
     * thousand of them across covers half the glass, not all of it.
     *
     * The second is TOSEMU_SCALE, which is how many of those an ST pixel
     * becomes. It has to be the same number a window magnifies by or the
     * window does not come out the size of the display, which is why it is one
     * setting and not two.
     */
    if (out_scale < 1)
        out_scale = 1;

    w = pixels_w / out_scale / screen_scale();
    h = pixels_h / out_scale / screen_scale();

    /*
     * Down to a multiple of sixteen across.
     *
     * A surface is planes and a plane is words, so a row is a whole number of
     * them: surface_create rounds the allocation up to one and the VDI's
     * v_lin_wr rounds the row length down, and a width between the two would
     * have them disagree about where the next row starts. Every screen an
     * Atari had is a multiple of sixteen and the question never came up.
     */
    w &= ~15;

    /* Nothing useful is smaller than the smallest screen an Atari had, and a
     * coordinate in GEM is a signed word */
    if (w < SCREEN_MIN_W)
        w = SCREEN_MIN_W;
    if (h < SCREEN_MIN_H)
        h = SCREEN_MIN_H;
    if (w > 32767)
        w = 32767 & ~15;
    if (h > 32767)
        h = 32767;

    *width = (int16_t)w;
    *height = (int16_t)h;
}

/* What the compositor said about one display */
struct display {
    char name[64];
    int32_t width, height;      /* the mode it is in, in its own pixels */
    int32_t scale;
    int known;                  /* whether a mode ever arrived for it */
};

#define DISPLAYS (8)

static struct {
    struct display display[DISPLAYS];
    int count;
} found;

/*
 * Which display an event is about is which listener it arrived at, because
 * wl_output does not say in the event itself. That is the third argument to
 * wl_output_add_listener and it comes back as the first argument here - not
 * wl_output_set_user_data, which adding a listener overwrites.
 */
static void output_geometry(void *data, struct wl_output *output,
                            int32_t x, int32_t y, int32_t pw, int32_t ph,
                            int32_t subpixel, const char *make,
                            const char *model, int32_t transform)
{
    (void)data; (void)output; (void)x; (void)y; (void)pw; (void)ph;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *output, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh)
{
    struct display *d = data;

    (void)output; (void)refresh;

    /* A display lists every mode it can do and says which one it is in */
    if (!(flags & WL_OUTPUT_MODE_CURRENT))
        return;

    d->width = width;
    d->height = height;
    d->known = 1;
}

static void output_done(void *data, struct wl_output *output)
{
    (void)data; (void)output;
}

static void output_scale(void *data, struct wl_output *output, int32_t factor)
{
    struct display *d = data;

    (void)output;

    d->scale = factor;
}

static void output_name(void *data, struct wl_output *output, const char *name)
{
    struct display *d = data;

    (void)output;

    snprintf(d->name, sizeof d->name, "%s", name ? name : "");
}

static void output_description(void *data, struct wl_output *output,
                               const char *description)
{
    (void)data; (void)output; (void)description;
}

static const struct wl_output_listener output_listener = {
    output_geometry,
    output_mode,
    output_done,
    output_scale,
    output_name,
    output_description
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t id, const char *interface,
                            uint32_t version)
{
    struct wl_output *output;
    struct display *d;
    uint32_t want;

    (void)data;

    if (strcmp(interface, wl_output_interface.name) != 0)
        return;

    if (found.count >= DISPLAYS)
        return;

    /*
     * Four is where the name arrives, which is the only thing a person can
     * pick a display out by. An older compositor still says how large it is,
     * so the modes work there and only the choosing does not.
     */
    want = version < 4 ? version : 4;

    output = wl_registry_bind(registry, id, &wl_output_interface, want);
    if (!output)
        return;

    d = &found.display[found.count++];
    d->scale = 1;

    wl_output_add_listener(output, &output_listener, d);
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t id)
{
    (void)data; (void)registry; (void)id;
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_remove
};

/*
 * Asks the compositor how large the display is.
 *
 * No window, and no surface of any kind: wl_output is a global like any other,
 * and what it has to say arrives on a roundtrip. That is what makes this
 * possible at all, because the screen has to exist before anything can be
 * shown in it - a window is opened onto a screen rather than the other way
 * about.
 *
 * A connection of its own rather than the one gfx.c makes, because the daemon
 * has no gfx.c and asks the same question. It is one round trip once, at the
 * moment the machine is being decided.
 */
static int ask_the_compositor(int32_t *pixels_w, int32_t *pixels_h,
                              int32_t *out_scale)
{
    struct wl_display *display;
    struct wl_registry *registry;
    const char *wanted = setting("TOSEMU_OUTPUT");
    struct display *picked = 0;
    int i;

    memset(&found, 0, sizeof found);

    display = wl_display_connect(0);
    if (!display)
        return 0;

    registry = wl_display_get_registry(display);
    if (!registry)
    {
        wl_display_disconnect(display);
        return 0;
    }

    wl_registry_add_listener(registry, &registry_listener, 0);

    wl_display_roundtrip(display);      /* which globals there are */
    wl_display_roundtrip(display);      /* and what each display says */

    wl_display_disconnect(display);

    for (i = 0; i < found.count; i++)
    {
        struct display *d = &found.display[i];

        if (!d->known)
            continue;

        if (wanted && *wanted)
        {
            if (strcmp(d->name, wanted) == 0)
            {
                picked = d;
                break;
            }
            continue;
        }

        /* Nobody said which, so the first one the compositor mentioned. On a
         * desk with two displays that is usually the one in front of you, and
         * TOSEMU_OUTPUT is there for when it is not. */
        picked = d;
        break;
    }

    if (!picked)
    {
        if (wanted && *wanted)
        {
            fprintf(stderr, "TOSEMU_OUTPUT: no display is called '%s'. "
                            "There is", wanted);
            for (i = 0; i < found.count; i++)
                fprintf(stderr, "%s %s", i ? "," : "",
                        found.display[i].name[0] ? found.display[i].name
                                                 : "one with no name");
            fprintf(stderr, "%s.\n", found.count ? "" : " none");
        }

        return 0;
    }

    *pixels_w = picked->width;
    *pixels_h = picked->height;
    *out_scale = picked->scale ? picked->scale : 1;

    return 1;
}

void screen_mode(int16_t *width, int16_t *height, int16_t *planes)
{
    const char *want = setting("TOSEMU_SCREEN");
    size_t i;

    for (i = 0; want && i < sizeof modes / sizeof modes[0]; i++)
    {
        if (strcmp(want, modes[i].name) != 0)
            continue;

        *width = modes[i].width;
        *height = modes[i].height;
        *planes = modes[i].planes;
        return;
    }

    for (i = 0; want && i < sizeof native / sizeof native[0]; i++)
    {
        int32_t pixels_w, pixels_h, out_scale;

        if (strcmp(want, native[i].name) != 0)
            continue;

        *planes = native[i].planes;

        if (ask_the_compositor(&pixels_w, &pixels_h, &out_scale))
        {
            screen_from_display(pixels_w, pixels_h, out_scale, width, height);
            return;
        }

        /*
         * Nobody to ask, which is every test run and every session with no
         * desktop in it. The size falls back to the one GEM applications were
         * written for and the planes stay as asked, because the planes are the
         * half of this that does not depend on there being a display.
         */
        *width = modes[MODE_DEFAULT].width;
        *height = modes[MODE_DEFAULT].height;
        return;
    }

    /* Said and not understood, which is worth a word: a misspelt screen that
     * quietly becomes the usual one is a machine that is not the one that was
     * asked for, and everything drawn on it is the wrong size */
    if (want)
    {
        fprintf(stderr, "TOSEMU_SCREEN: no screen is called '%s'. There is",
                want);
        for (i = 0; i < sizeof modes / sizeof modes[0]; i++)
            fprintf(stderr, "%s %s", i ? "," : "", modes[i].name);
        for (i = 0; i < sizeof native / sizeof native[0]; i++)
            fprintf(stderr, ", %s", native[i].name);
        fprintf(stderr, ".\n");
    }

    *width = modes[MODE_DEFAULT].width;
    *height = modes[MODE_DEFAULT].height;
    *planes = modes[MODE_DEFAULT].planes;
}
