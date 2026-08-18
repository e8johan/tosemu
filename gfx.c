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
#include <xkbcommon/xkbcommon.h>
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

    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;

    struct xkb_context *xkb;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;

    /* Keys waiting to be read, oldest first */
    uint16_t keys[32];
    int key_count;

    int16_t mouse_x, mouse_y;   /* In surface pixels, not window ones */
    int16_t buttons;
    int buttons_changed;

    int configured;
    int closed;
    int showing;
} w;

/* Input *******************************************************************/

/*
 * The scan code of a key, which is the half of it that says which key rather
 * than which letter.
 *
 * Most of them need no table at all. An ST's keyboard reports the IBM XT scan
 * codes for its main block, and so does a Linux evdev keyboard, so the number
 * that arrives from the compositor is already the number GEM expects: S is
 * 0x1f on both. The range below 0x54 is the block where that holds.
 *
 * What does not hold is the cursor and editing keys. An ST has them where a PC
 * has its keypad, and evdev gives them numbers of their own well above the
 * block, so those are looked up by what they mean rather than by where they
 * are.
 * http://toshyp.atari.org/en/003007.html
 */
static uint16_t scancode_for(xkb_keysym_t sym)
{
    switch (sym)
    {
        case XKB_KEY_Escape:    return 0x01;
        case XKB_KEY_BackSpace: return 0x0e;
        case XKB_KEY_Tab:       return 0x0f;
        case XKB_KEY_Return:    return 0x1c;
        case XKB_KEY_KP_Enter:  return 0x72;
        case XKB_KEY_Delete:    return 0x53;
        case XKB_KEY_Insert:    return 0x52;
        case XKB_KEY_Home:      return 0x47;
        case XKB_KEY_Up:        return 0x48;
        case XKB_KEY_Left:      return 0x4b;
        case XKB_KEY_Right:     return 0x4d;
        case XKB_KEY_Down:      return 0x50;
        case XKB_KEY_F1:        return 0x3b;
        case XKB_KEY_F2:        return 0x3c;
        case XKB_KEY_F3:        return 0x3d;
        case XKB_KEY_F4:        return 0x3e;
        case XKB_KEY_F5:        return 0x3f;
        case XKB_KEY_F6:        return 0x40;
        case XKB_KEY_F7:        return 0x41;
        case XKB_KEY_F8:        return 0x42;
        case XKB_KEY_F9:        return 0x43;
        case XKB_KEY_F10:       return 0x44;
        /* An ST has Undo and Help where a modern keyboard has neither, so the
         * two least missed keys stand in for them */
        case XKB_KEY_End:       return 0x61;    /* Undo */
        case XKB_KEY_Page_Down: return 0x62;    /* Help */
        default:                return 0;
    }
}

static void key_post(uint16_t key)
{
    if (w.key_count >= (int)(sizeof w.keys / sizeof w.keys[0]))
        return;     /* Typing faster than the application reads */

    w.keys[w.key_count++] = key;
}

int gfx_key_take(uint16_t *key)
{
    int i;

    if (w.key_count == 0)
        return 0;

    *key = w.keys[0];

    for (i = 1; i < w.key_count; i++)
        w.keys[i-1] = w.keys[i];
    w.key_count--;

    return 1;
}

void gfx_mouse(int16_t *x, int16_t *y, int16_t *buttons)
{
    *x = w.mouse_x;
    *y = w.mouse_y;
    *buttons = w.buttons;
}

uint16_t gfx_kstate()
{
    uint16_t state = 0;

    if (!w.xkb_state)
        return 0;

    /* The bits GEM uses: right shift, left shift, control, alt */
    if (xkb_state_mod_name_is_active(w.xkb_state, XKB_MOD_NAME_SHIFT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        state |= 0x0003;
    if (xkb_state_mod_name_is_active(w.xkb_state, XKB_MOD_NAME_CTRL,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        state |= 0x0004;
    if (xkb_state_mod_name_is_active(w.xkb_state, XKB_MOD_NAME_ALT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0)
        state |= 0x0008;

    return state;
}

int gfx_buttons_changed()
{
    int changed = w.buttons_changed;

    w.buttons_changed = 0;

    return changed;
}

/* Keyboard ****************************************************************/

static void kb_keymap(void *data, struct wl_keyboard *kb, uint32_t format,
                      int32_t fd, uint32_t size)
{
    char *text;

    (void)data; (void)kb;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    {
        close(fd);
        return;
    }

    text = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (text != MAP_FAILED)
    {
        if (w.keymap)
            xkb_keymap_unref(w.keymap);
        if (w.xkb_state)
            xkb_state_unref(w.xkb_state);

        w.keymap = xkb_keymap_new_from_string(w.xkb, text,
                                              XKB_KEYMAP_FORMAT_TEXT_V1,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS);
        w.xkb_state = w.keymap ? xkb_state_new(w.keymap) : 0;

        munmap(text, size);
    }

    close(fd);
}

static void kb_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
                     struct wl_surface *s, struct wl_array *keys)
{
    (void)data; (void)kb; (void)serial; (void)s; (void)keys;
}

static void kb_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
                     struct wl_surface *s)
{
    (void)data; (void)kb; (void)serial; (void)s;
}

static void kb_key(void *data, struct wl_keyboard *kb, uint32_t serial,
                   uint32_t time, uint32_t key, uint32_t state)
{
    xkb_keycode_t code = key + 8;   /* Wayland counts from a different place */
    xkb_keysym_t sym;
    uint16_t scan, ch = 0;
    char utf8[8];

    (void)data; (void)kb; (void)serial; (void)time;

    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !w.xkb_state)
        return;

    sym = xkb_state_key_get_one_sym(w.xkb_state, code);

    /*
     * The number the compositor gave, when it is one the ST would have given
     * too. A GEM application reads this half to tell keys apart: a menu
     * shortcut is a scan code, not a letter, so leaving it empty makes every
     * shortcut in every application unreachable.
     */
    if (key >= 1 && key < 0x54)
        scan = (uint16_t)key;
    else
        scan = scancode_for(sym);

    /* Anything that types a single byte types it. GEM predates any of the
     * ways of saying more than one. */
    if (xkb_state_key_get_utf8(w.xkb_state, code, utf8, sizeof utf8) == 1)
        ch = (unsigned char)utf8[0];

    if (scan == 0 && ch == 0)
        return;     /* A key GEM has no way of describing */

    key_post((uint16_t)((scan << 8) | ch));
}

static void kb_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
                         uint32_t depressed, uint32_t latched, uint32_t locked,
                         uint32_t group)
{
    (void)data; (void)kb; (void)serial;

    if (w.xkb_state)
        xkb_state_update_mask(w.xkb_state, depressed, latched, locked,
                              0, 0, group);
}

static void kb_repeat(void *data, struct wl_keyboard *kb, int32_t rate,
                      int32_t delay)
{
    (void)data; (void)kb; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat
};

/* Pointer *****************************************************************/

static void pt_enter(void *data, struct wl_pointer *p, uint32_t serial,
                     struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
    (void)data; (void)p; (void)serial; (void)s;

    w.mouse_x = (int16_t)(wl_fixed_to_int(x) / SCALE);
    w.mouse_y = (int16_t)(wl_fixed_to_int(y) / SCALE);
}

static void pt_leave(void *data, struct wl_pointer *p, uint32_t serial,
                     struct wl_surface *s)
{
    (void)data; (void)p; (void)serial; (void)s;
}

static void pt_motion(void *data, struct wl_pointer *p, uint32_t time,
                      wl_fixed_t x, wl_fixed_t y)
{
    (void)data; (void)p; (void)time;

    /* Out of the window's pixels and back into the screen's */
    w.mouse_x = (int16_t)(wl_fixed_to_int(x) / SCALE);
    w.mouse_y = (int16_t)(wl_fixed_to_int(y) / SCALE);
}

static void pt_button(void *data, struct wl_pointer *p, uint32_t serial,
                      uint32_t time, uint32_t button, uint32_t state)
{
    int16_t bit;

    (void)data; (void)p; (void)serial; (void)time;

    /* GEM numbers them from the left, and has two */
    switch (button)
    {
        case 0x110: bit = 1; break;     /* BTN_LEFT */
        case 0x111: bit = 2; break;     /* BTN_RIGHT */
        default: return;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        w.buttons |= bit;
    else
        w.buttons &= ~bit;

    w.buttons_changed = 1;
}

static void pt_axis(void *data, struct wl_pointer *p, uint32_t time,
                    uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)p; (void)time; (void)axis; (void)value;
}

/*
 * Everything a version 5 pointer can say that GEM has no use for. They have to
 * be here rather than left null: the version bound decides which of these the
 * compositor may call, and calling a null one is not a missing feature but a
 * crash.
 */
static void pt_frame(void *data, struct wl_pointer *p)
{
    (void)data; (void)p;
}

static void pt_axis_source(void *data, struct wl_pointer *p, uint32_t source)
{
    (void)data; (void)p; (void)source;
}

static void pt_axis_stop(void *data, struct wl_pointer *p, uint32_t time,
                         uint32_t axis)
{
    (void)data; (void)p; (void)time; (void)axis;
}

static void pt_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis,
                             int32_t discrete)
{
    (void)data; (void)p; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    pt_enter, pt_leave, pt_motion, pt_button, pt_axis,
    pt_frame, pt_axis_source, pt_axis_stop, pt_axis_discrete
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    (void)data;

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !w.keyboard)
    {
        w.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(w.keyboard, &keyboard_listener, 0);
    }

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !w.pointer)
    {
        w.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(w.pointer, &pointer_listener, 0);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name
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
    else if (!strcmp(interface, wl_seat_interface.name))
    {
        w.seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
        wl_seat_add_listener(w.seat, &seat_listener, 0);
    }
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

    w.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

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
    if (w.xkb_state)
        xkb_state_unref(w.xkb_state);
    if (w.keymap)
        xkb_keymap_unref(w.keymap);
    if (w.xkb)
        xkb_context_unref(w.xkb);
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
