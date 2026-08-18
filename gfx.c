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
 * The window the emulated screen is shown in.
 *
 * A surface is Atari memory: planes, a word of each in turn, and a palette of
 * colour registers. A compositor wants none of that, so this is where the two
 * meet - the planes are gathered into colours, the colours looked up, and the
 * result written into memory the compositor can read.
 *
 * The scaling is done here rather than left to the compositor. An ST pixel is
 * not a small modern pixel, it is a large old one, and the only honest way to
 * make it large again is to repeat it. A compositor asked to scale would
 * smooth it, which is the one thing it must not do.
 *
 * Three coordinate spaces meet in this file, and surface.h names them: the
 * surface's own pixels, those multiplied by the scale, and what the compositor
 * deals in. Nothing here mixes them without saying so.
 */

/* memfd_create is a GNU extension, and this has to be said before the first
 * include rather than merely before the one that declares it */
#define _GNU_SOURCE

#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

#include "surface.h"
#include "emuvdi/emuvdi.h"

/* How much larger than an ST pixel one on the screen is */
#define SCALE (3)

static struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;

    struct wl_buffer *buffer;
    uint32_t *pixels;
    size_t bytes;

    struct surface *screen;
    int width, height;      /* In compositor pixels, so already scaled */

    int configured;
    int closed;
    int showing;
} w;

/* Wayland asks to be told the connection is still wanted */
static void wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    wm_base_ping
};

/*
 * A configure says the window may now be drawn, and has to be acknowledged
 * before anything is attached to it
 */
static void surface_configure(void *data, struct xdg_surface *s, uint32_t serial)
{
    (void)data;
    xdg_surface_ack_configure(s, serial);
    w.configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    surface_configure
};

static void toplevel_configure(void *data, struct xdg_toplevel *t,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    /*
     * The compositor is entitled to a say in how large the window is. What it
     * is not entitled to is a stretched picture, so the size it asks for is
     * noted and the picture stays the size it is - a window larger than the
     * screen it shows has a border, and one smaller shows less of it.
     */
    (void)data; (void)t; (void)width; (void)height; (void)states;
}

static void toplevel_close(void *data, struct xdg_toplevel *t)
{
    (void)data; (void)t;
    w.closed = 1;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure,
    toplevel_close
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    (void)data; (void)version;

    if (!strcmp(interface, wl_compositor_interface.name))
        w.compositor = wl_registry_bind(registry, name,
                                        &wl_compositor_interface, 4);
    else if (!strcmp(interface, wl_shm_interface.name))
        w.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, xdg_wm_base_interface.name))
        w.wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
}

static void registry_remove(void *data, struct wl_registry *r, uint32_t name)
{
    (void)data; (void)r; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_remove
};

/* Memory both this and the compositor can see, which is how a picture is
 * handed over without copying it through the socket */
static int make_buffer(void)
{
    struct wl_shm_pool *pool;
    int fd;

    w.bytes = (size_t)w.width * w.height * 4;

    fd = memfd_create("tosemu-screen", MFD_CLOEXEC);
    if (fd < 0)
        return 0;

    if (ftruncate(fd, (off_t)w.bytes) < 0)
    {
        close(fd);
        return 0;
    }

    w.pixels = mmap(0, w.bytes, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (w.pixels == MAP_FAILED)
    {
        w.pixels = 0;
        close(fd);
        return 0;
    }

    pool = wl_shm_create_pool(w.shm, fd, (int32_t)w.bytes);
    w.buffer = wl_shm_pool_create_buffer(pool, 0, w.width, w.height,
                                         w.width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    return w.buffer != 0;
}

int gfx_open(struct surface *screen)
{
    memset(&w, 0, sizeof w);

    /*
     * A way to say no. The tests run GEM programs, and a test suite that
     * opens and closes windows on whoever's desktop happens to be logged in
     * is a nuisance rather than a feature.
     */
    if (getenv("TOSEMU_NO_WINDOW"))
        return 0;

    w.screen = screen;
    w.width = surface_width(screen) * SCALE;
    w.height = surface_height(screen) * SCALE;

    w.display = wl_display_connect(0);
    if (!w.display)
        return 0;   /* Nobody is logged in, which is the ordinary case here */

    w.registry = wl_display_get_registry(w.display);
    wl_registry_add_listener(w.registry, &registry_listener, 0);
    wl_display_roundtrip(w.display);

    if (!w.compositor || !w.shm || !w.wm_base)
    {
        printf("GFX: the compositor is missing something this needs\n");
        wl_display_disconnect(w.display);
        w.display = 0;
        return 0;
    }

    xdg_wm_base_add_listener(w.wm_base, &wm_base_listener, 0);

    w.surface = wl_compositor_create_surface(w.compositor);
    w.xdg_surface = xdg_wm_base_get_xdg_surface(w.wm_base, w.surface);
    xdg_surface_add_listener(w.xdg_surface, &xdg_surface_listener, 0);

    w.toplevel = xdg_surface_get_toplevel(w.xdg_surface);
    xdg_toplevel_add_listener(w.toplevel, &toplevel_listener, 0);
    xdg_toplevel_set_title(w.toplevel, "TOS");
    xdg_toplevel_set_app_id(w.toplevel, "se.e8johan.tosemu");

    wl_surface_commit(w.surface);
    wl_display_roundtrip(w.display);    /* Waits for the first configure */

    if (!make_buffer())
    {
        printf("GFX: no room for a %dx%d window\n", w.width, w.height);
        gfx_close();
        return 0;
    }

    w.showing = 1;

    return 1;
}

void gfx_close()
{
    if (w.pixels)
        munmap(w.pixels, w.bytes);
    if (w.buffer)
        wl_buffer_destroy(w.buffer);
    if (w.toplevel)
        xdg_toplevel_destroy(w.toplevel);
    if (w.xdg_surface)
        xdg_surface_destroy(w.xdg_surface);
    if (w.surface)
        wl_surface_destroy(w.surface);
    if (w.display)
        wl_display_disconnect(w.display);

    memset(&w, 0, sizeof w);
}

int gfx_showing()
{
    return w.showing && !w.closed;
}

int gfx_fd()
{
    return w.display ? wl_display_get_fd(w.display) : -1;
}

void gfx_dispatch()
{
    if (!w.display)
        return;

    if (wl_display_dispatch(w.display) < 0)
        w.closed = 1;
}

void gfx_flush()
{
    if (w.display)
        wl_display_flush(w.display);
}

void gfx_present()
{
    int x, y, sx, sy;
    int sw, sh;

    if (!gfx_showing() || !w.configured)
        return;

    sw = surface_width(w.screen);
    sh = surface_height(w.screen);

    /*
     * A pixel at a time, through the palette and out to as many pixels as the
     * scale asks for. This is the whole picture every time rather than the
     * part that changed: at ST sizes it is a few hundred thousand writes, and
     * knowing what changed is worth having only once there is something to
     * spend the saving on.
     */
    for (y = 0; y < sh; y++)
    {
        for (x = 0; x < sw; x++)
        {
            uint32_t argb = emuvdi_palette_argb(surface_pixel(w.screen, x, y));

            for (sy = 0; sy < SCALE; sy++)
            {
                uint32_t *row = w.pixels + (size_t)(y*SCALE + sy) * w.width
                              + x*SCALE;

                for (sx = 0; sx < SCALE; sx++)
                    row[sx] = argb;
            }
        }
    }

    wl_surface_attach(w.surface, w.buffer, 0, 0);
    wl_surface_damage_buffer(w.surface, 0, 0, w.width, w.height);
    wl_surface_commit(w.surface);
    wl_display_flush(w.display);
}
