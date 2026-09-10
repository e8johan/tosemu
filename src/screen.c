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

#ifndef NO_WAYLAND
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

/* For the wait that gives up - see wait_for */
#include <errno.h>
#include <poll.h>
#include <time.h>
#endif

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
 * And the four that are a rule rather than a size: as much room as there is,
 * in colour or in black and white.
 *
 * They are not Atari screens and are not pretending to be. What they are for
 * is a GEM application having the room a modern display has, which is the one
 * thing the machine could not give it - the rest of GEM does not mind, because
 * a resource is measured in characters and more of them across is simply more
 * room.
 *
 * What "will hold" means is the difference between the two pairs. A desktop
 * keeps some of its display for itself - a panel across the bottom, a dock
 * down one side - and a screen worked out from the whole display is a screen
 * whose bottom right corner is behind that panel, which is where a GEM
 * window's size box is. So native-mono and native-color are as large as a
 * window may be, which is what maximising one answers; display-mono and
 * display-color are the display itself, panel or no panel, which is what
 * these two meant before there was any way to tell the difference.
 */
static const struct {
    const char *name;
    int16_t planes;
    int work;               /* the room a window has rather than the display */
} native[] = {
    { "native-mono",   1, 1 },
    { "native-color",  4, 1 },
    { "display-mono",  1, 0 },
    { "display-color", 4, 0 },
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

void screen_inset(const struct screen_display *displays, int count,
                  int32_t maximized_w, int32_t maximized_h,
                  int32_t *inset_w, int32_t *inset_h)
{
    int32_t least = -1;
    int i;

    *inset_w = 0;
    *inset_h = 0;

    /* A compositor that maximised a window to nothing has said nothing */
    if (maximized_w <= 0 || maximized_h <= 0)
        return;

    for (i = 0; i < count; i++)
    {
        int32_t scale = displays[i].scale > 0 ? displays[i].scale : 1;
        int32_t w = displays[i].width / scale - maximized_w;
        int32_t h = displays[i].height / scale - maximized_h;

        /* A window is not maximised to more than the display it is on, so a
         * display it does not fit inside is not the display it went to */
        if (w < 0 || h < 0)
            continue;

        if (least < 0 || w + h < least)
        {
            least = w + h;
            *inset_w = w;
            *inset_h = h;
        }
    }
}

#ifndef NO_WAYLAND

/* What the compositor said about one display */
struct display {
    char name[64];
    struct screen_display size;
    int known;                  /* whether a mode ever arrived for it */
};

#define DISPLAYS (8)

static struct {
    struct display display[DISPLAYS];
    int count;

    /* And what is needed to ask the other question, which is how large a
     * window the desktop leaves room for - see ask_for_maximized */
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
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

    d->size.width = width;
    d->size.height = height;
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

    d->size.scale = factor;
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

/*
 * How large a window the desktop leaves room for, which is not a thing
 * Wayland has an answer to.
 *
 * There is no work area in the protocol. A compositor says how large its
 * displays are and never what it has put on them, so a panel across the
 * bottom is invisible to a client - and a screen worked out from the whole
 * display is a screen whose last rows are behind that panel. Which is where a
 * GEM window keeps its size box.
 *
 * What there is instead is maximising, which is the compositor being asked for
 * the largest window it is willing to give and answering with a size. That
 * answer is the panel, the dock and the frame it draws itself, all subtracted
 * by the one party that knows about them.
 *
 * It is asked without a window ever appearing. A surface is made, a shell is
 * asked for a toplevel, the toplevel is asked to be maximised and the surface
 * is committed with nothing attached to it - which is the handshake every
 * Wayland window begins with, and a surface with no buffer is not shown. The
 * configure that comes back carries the size. Then all three are destroyed,
 * and nothing was ever on the display.
 */
struct maximized {
    int32_t width, height;
    int configured;
};

/* Wayland asks to be told the connection is still wanted */
static void wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    wm_base_ping
};

/* The size arrives on the toplevel and the go-ahead on the surface under it,
 * in that order, so this is what says the answer is complete */
static void probe_surface_configure(void *data, struct xdg_surface *surface,
                                    uint32_t serial)
{
    struct maximized *m = data;

    xdg_surface_ack_configure(surface, serial);
    m->configured = 1;
}

static const struct xdg_surface_listener probe_surface_listener = {
    probe_surface_configure
};

static void probe_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                     int32_t width, int32_t height,
                                     struct wl_array *states)
{
    struct maximized *m = data;

    (void)toplevel; (void)states;

    /* The states say maximised, which is the only thing that was asked for
     * and so the only thing it can be. What is wanted is the size. */
    m->width = width;
    m->height = height;
}

static void probe_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    (void)data; (void)toplevel;
}

static const struct xdg_toplevel_listener probe_toplevel_listener = {
    probe_toplevel_configure,
    probe_toplevel_close
};

/* Long enough for an answer that is worked out in microseconds, short enough
 * that a compositor which never answers is a pause and not a hang */
#define MAXIMIZED_MS (250)

/*
 * Waiting for one thing to arrive, or for the time to be up.
 *
 * wl_display_roundtrip is the usual way and is not this way: it waits for as
 * long as it takes, and what is being waited for here is a courtesy - the
 * screen is worked out without it, and a compositor that has stopped talking
 * should cost the emulator a quarter of a second at startup rather than the
 * startup itself. So the reading is done by hand, which is the only way to
 * put a clock on it.
 */
static int wait_for(struct wl_display *display, const int *done, int ms)
{
    struct timespec started;
    int fd = wl_display_get_fd(display);

    clock_gettime(CLOCK_MONOTONIC, &started);

    while (!*done)
    {
        struct pollfd waiting;
        struct timespec now;
        long left;
        int ready;

        /*
         * Wayland's own dance for reading events without racing anything
         * else that might be reading them. Nothing else is, here - this
         * connection is this function's - but prepare_read is also what says
         * whether there are events already in hand, and those have to be
         * handed out before the wait, or the answer could be sitting in the
         * queue while poll waits for another.
         */
        while (wl_display_prepare_read(display) != 0)
        {
            if (wl_display_dispatch_pending(display) < 0)
                return 0;
        }

        if (wl_display_flush(display) < 0 && errno != EAGAIN)
        {
            wl_display_cancel_read(display);
            return 0;
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        left = ms - ((now.tv_sec - started.tv_sec) * 1000
                     + (now.tv_nsec - started.tv_nsec) / 1000000);
        if (left < 0)
            left = 0;

        waiting.fd = fd;
        waiting.events = POLLIN;
        waiting.revents = 0;

        ready = poll(&waiting, 1, (int)left);

        if (ready < 0 && errno == EINTR)
        {
            wl_display_cancel_read(display);
            continue;
        }

        if (ready <= 0)
        {
            wl_display_cancel_read(display);
            return 0;
        }

        if (wl_display_read_events(display) < 0)
            return 0;

        if (wl_display_dispatch_pending(display) < 0)
            return 0;
    }

    return 1;
}

/* Nought for both when there is nobody to ask, nothing to ask with, or no
 * answer in time - which the caller reads as the desktop keeping none of the
 * display for itself, and so as the display it already has */
static void ask_for_maximized(struct wl_display *display,
                              int32_t *width, int32_t *height)
{
    struct maximized m;
    struct wl_surface *surface;
    struct xdg_surface *shell_surface;
    struct xdg_toplevel *toplevel;

    *width = 0;
    *height = 0;

    /* A compositor with no xdg-shell in it is not one that maximises
     * anything, and there were desktops like that */
    if (!found.compositor || !found.wm_base)
        return;

    memset(&m, 0, sizeof m);

    surface = wl_compositor_create_surface(found.compositor);
    if (!surface)
        return;

    shell_surface = xdg_wm_base_get_xdg_surface(found.wm_base, surface);
    if (!shell_surface)
    {
        wl_surface_destroy(surface);
        return;
    }

    xdg_surface_add_listener(shell_surface, &probe_surface_listener, &m);

    toplevel = xdg_surface_get_toplevel(shell_surface);
    if (!toplevel)
    {
        xdg_surface_destroy(shell_surface);
        wl_surface_destroy(surface);
        return;
    }

    xdg_toplevel_add_listener(toplevel, &probe_toplevel_listener, &m);

    /* Named as the windows are, because a desktop is entitled to have rules
     * about a particular application and the answer should be the one the
     * emulator's own windows would get */
    xdg_toplevel_set_app_id(toplevel, "se.e8johan.tosemu");
    xdg_toplevel_set_title(toplevel, "tosemu");

    xdg_toplevel_set_maximized(toplevel);

    /* No buffer, so no window: this is the question and not the showing */
    wl_surface_commit(surface);

    if (wait_for(display, &m.configured, MAXIMIZED_MS)
        && m.width > 0 && m.height > 0)
    {
        *width = m.width;
        *height = m.height;
    }

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(shell_surface);
    wl_surface_destroy(surface);
}

/* screen_inset over the displays this connection heard about, which are the
 * ones a window could have been maximised onto */
static void inset_of_found(int32_t maximized_w, int32_t maximized_h,
                           int32_t *inset_w, int32_t *inset_h)
{
    struct screen_display known[DISPLAYS];
    int count = 0;
    int i;

    for (i = 0; i < found.count; i++)
        if (found.display[i].known)
            known[count++] = found.display[i].size;

    screen_inset(known, count, maximized_w, maximized_h, inset_w, inset_h);
}

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t id, const char *interface,
                            uint32_t version)
{
    struct wl_output *output;
    struct display *d;
    uint32_t want;

    (void)data;

    /*
     * The two globals the maximised size is asked through, at the lowest
     * version that answers: a surface is made and a shell is asked what size
     * it would give it, and neither of those grew a new way of being done.
     * gfx.c binds the same shell at the same version, which is what keeps its
     * listener two entries long and this one likewise.
     */
    if (strcmp(interface, wl_compositor_interface.name) == 0)
    {
        found.compositor = wl_registry_bind(registry, id,
                                            &wl_compositor_interface, 1);
        return;
    }

    if (strcmp(interface, xdg_wm_base_interface.name) == 0)
    {
        found.wm_base = wl_registry_bind(registry, id,
                                         &xdg_wm_base_interface, 1);
        if (found.wm_base)
            xdg_wm_base_add_listener(found.wm_base, &wm_base_listener, 0);
        return;
    }

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
    d->size.scale = 1;

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
 * Asks the compositor how large the display is, and how much of it a window
 * may have.
 *
 * No window, and until the second question no surface of any kind: wl_output
 * is a global like any other, and what it has to say arrives on a roundtrip.
 * That is what makes this possible at all, because the screen has to exist
 * before anything can be shown in it - a window is opened onto a screen rather
 * than the other way about. The surface the second question needs is never
 * shown either; see ask_for_maximized.
 *
 * A connection of its own rather than the one gfx.c makes, because the daemon
 * has no gfx.c and asks the same question. It is two round trips and a wait
 * once, at the moment the machine is being decided.
 */
static int ask_the_compositor(int want_work, int32_t *pixels_w,
                              int32_t *pixels_h, int32_t *out_scale)
{
    struct wl_display *display;
    struct wl_registry *registry;
    const char *wanted = setting("TOSEMU_OUTPUT");
    struct display *picked = 0;
    int32_t maximized_w = 0, maximized_h = 0;
    int32_t inset_w = 0, inset_h = 0;
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

    if (want_work)
        ask_for_maximized(display, &maximized_w, &maximized_h);

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

    *pixels_w = picked->size.width;
    *pixels_h = picked->size.height;
    *out_scale = picked->size.scale ? picked->size.scale : 1;

    if (maximized_w <= 0 || maximized_h <= 0)
        return 1;               /* the display as it is, which is all there is */

    /*
     * Nobody named a display, so the maximised size is the answer as it
     * stands - and the display it is about is the one the compositor puts a
     * new window on, which is the one the emulator's windows are about to
     * appear on as well. It is in the pixels a window is measured in, which
     * is what a scale of one says here: the compositor has already divided by
     * its own, that being what it did to arrive at the size it offered.
     */
    if (!(wanted && *wanted))
    {
        *pixels_w = maximized_w;
        *pixels_h = maximized_h;
        *out_scale = 1;

        return 1;
    }

    /*
     * Somebody did, and a window cannot be maximised onto a display of one's
     * choosing: xdg-shell has set_fullscreen for a named display and nothing
     * of the kind for maximising, so the window went wherever the compositor
     * puts windows. What carries across is how much room the desktop took,
     * which is the same panel on every display of a desk that has one, so
     * that is what is worked out and taken off the display that was asked
     * for.
     */
    inset_of_found(maximized_w, maximized_h, &inset_w, &inset_h);

    *pixels_w -= inset_w * *out_scale;
    *pixels_h -= inset_h * *out_scale;

    return 1;
}

#else /* NO_WAYLAND */

/*
 * Nobody to ask, and no way to ask built in.
 *
 * This is the answer a build with Wayland in it also arrives at whenever there
 * is no compositor running, so the caller has nothing new to handle: the
 * screen falls back to the one GEM applications were written for and the
 * planes stay as asked. See screen_mode below.
 */
static int ask_the_compositor(int want_work, int32_t *pixels_w,
                              int32_t *pixels_h, int32_t *out_scale)
{
    (void)want_work; (void)pixels_w; (void)pixels_h; (void)out_scale;

    return 0;
}

#endif /* NO_WAYLAND */

/*
 * The same question, asked once.
 *
 * The machine is worked out more than once in a run - the screen is reserved
 * before a program is loaded, and GEM asks again when it starts and needs to
 * know how large a surface to make - and a display is not unplugged in
 * between. Remembering the answer keeps the round trip the one round trip the
 * note above describes rather than one per caller.
 *
 * The one thing that makes it worth asking twice is a caller wanting the
 * maximised size when the answer in hand was worked out without it. That
 * cannot happen as things are - every caller in a run asks about the same
 * screen - but an answer that is remembered has to say what question it
 * answers, or the first caller decides for the rest.
 */
static int ask_the_compositor_once(int want_work, int32_t *pixels_w,
                                   int32_t *pixels_h, int32_t *out_scale)
{
    static int asked;
    static int asked_work;
    static int answered;
    static int32_t was_w, was_h, was_scale;

    if (!asked || (want_work && !asked_work))
    {
        answered = ask_the_compositor(want_work, &was_w, &was_h, &was_scale);
        asked = 1;
        asked_work = want_work;
    }

    *pixels_w = was_w;
    *pixels_h = was_h;
    *out_scale = was_scale;

    return answered;
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

        if (ask_the_compositor_once(native[i].work, &pixels_w, &pixels_h,
                                    &out_scale))
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
