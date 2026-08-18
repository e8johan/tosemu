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
 * GEM's windows, shown as windows of the desktop's.
 *
 * The emulated screen is never shown. It is a coordinate space and a piece of
 * memory: the AES lays windows out in it and everything is drawn into it, the
 * way it always was, and an application cannot tell the difference. What is
 * shown are the windows - a window of the desktop's for each window GEM opens,
 * and one for each dialog - with the desktop itself showing through where an
 * ST would have had a grey background.
 *
 * That split is invisible from the other side. An application asks for a
 * rectangle and draws in it; where the rectangle is being shown, and whether
 * the person watching has since dragged it somewhere else, is not something it
 * can observe or needs to. It is the same reason the AES keeps a coordinate
 * space of its own rather than asking the desktop where anything is - see
 * aeswind.c - and it is what lets a modal dialog be dragged about while the
 * application inside it is blocked waiting for a button.
 *
 * A surface is Atari memory: planes, a word of each in turn, and a palette of
 * colour registers. Gathering the planes into colours and looking them up is
 * the other thing that happens here.
 *
 * The scaling is done here rather than handed to the compositor. An ST pixel
 * is not a small modern pixel, it is a large old one, and the only honest way
 * to make it large again is to repeat it. A compositor asked to scale would
 * smooth it, which is the one thing it must not do.
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

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-dialog-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

#include "surface.h"
#include "emuvdi/emuvdi.h"

/* Told to the AES when a window's frame is used to close it, so that the
 * application is sent the message it would have got from its own close box */
void host_window_closed(int16_t handle);

/* How much larger than an ST pixel one on the screen is */
#define SCALE (3)

/*
 * A window, which shows one rectangle of a surface. A GEM window is one of
 * these and so is a dialog; everything about showing something is the same for
 * both, which is why it is a structure rather than a special case.
 */
struct window {
    int used;
    int configured;

    /* The AES's handle for it, or 0 for the dialog, which the AES does not
     * give a handle to */
    int16_t handle;

    /* Which surface it shows part of. The screen shows the screen; a dialog
     * shows one of its own, so that what it draws does not also appear in the
     * window behind it. */
    struct surface *shows;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct xdg_dialog_v1 *dialog;
    struct zxdg_toplevel_decoration_v1 *decoration;

    struct wl_buffer *buffer;
    uint32_t *pixels;
    size_t bytes;

    /* The part of the screen it shows, in the screen's own pixels */
    int16_t sx, sy, sw, sh;

    /* And how large that is once scaled, which is what the compositor sees */
    int width, height;
};

/*
 * Slot 0 is the dialog, because there is one of those at a time. The rest are
 * GEM windows, which the AES allows eight of.
 */
#define DIALOG  (0)
#define WINDOWS (9)

static struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct xdg_wm_dialog_v1 *wm_dialog;
    struct zxdg_decoration_manager_v1 *decorations;

    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;

    struct xkb_context *xkb;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;

    struct window windows[WINDOWS];

    /* Which window the pointer is in, so that where it is can be given in the
     * screen's coordinates rather than in that window's */
    struct window *pointer_in;

    struct surface *screen;

    /* Keys waiting to be read, oldest first */
    uint16_t keys[32];
    int key_count;

    int16_t mouse_x, mouse_y;   /* In the screen's pixels, not a window's */
    int16_t buttons;

    /*
     * Every time the buttons change, rather than what they are now.
     *
     * The AES waits for a press and then for the release, and works out what
     * was clicked from the two. Reporting only the state loses that: a click
     * quick enough for both to arrive before anyone looks reads as a button
     * that is up, and the press it was waiting for never happened. So each
     * change is kept, with where the pointer was when it happened, and handed
     * over one at a time.
     */
    struct {
        int16_t buttons;
        int16_t x, y;
    } clicks[32];
    int click_count;

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
        /*
         * An ST has Undo and Help, and a modern keyboard has neither. Print
         * Screen and Scroll Lock stand in for them: both are within reach and
         * neither means anything to a GEM application otherwise.
         *
         * A compositor may well want Print Screen for itself, in which case it
         * never arrives here and Undo has to be reached some other way. That
         * is the compositor's to decide, not ours.
         */
        case XKB_KEY_Print:
        case XKB_KEY_Sys_Req:     return 0x61;  /* Undo */
        case XKB_KEY_Scroll_Lock: return 0x62;  /* Help */
        default:                return 0;
    }
}

static void key_post(uint16_t key)
{
    if (w.key_count >= (int)(sizeof w.keys / sizeof w.keys[0]))
        return;     /* Typing faster than the application reads */

    w.keys[w.key_count++] = key;
}

/*
 * Keys asked for on the command line, for when nothing is going to be typed.
 *
 * A dialog cannot be tested without something pressing a button in it, and a
 * test suite has nobody to do the pressing. TOSEMU_KEYS is a run of characters
 * to hand over as though they had been, and \r stands for Return, which is
 * what dismisses a dialog by its default button.
 */
static void keys_from_environment(void)
{
    static int done;
    const char *keys = getenv("TOSEMU_KEYS");
    int i;

    if (done || !keys)
        return;

    done = 1;

    for (i = 0; keys[i]; i++)
    {
        char c = keys[i];
        uint16_t scan = 0;

        if (c == '\\' && keys[i+1] == 'r')
        {
            c = '\r';
            scan = 0x1c;    /* Return, which a dialog looks at */
            i++;
        }

        key_post((uint16_t)((scan << 8) | (unsigned char)c));
    }
}

static void clicks_from_environment(void);

int gfx_key_take(uint16_t *key)
{
    int i;

    keys_from_environment();

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
    clicks_from_environment();

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

/*
 * Clicks asked for on the command line, for when nobody is going to click.
 *
 * TOSEMU_CLICKS is a list of places to press and release the left button, as
 * x,y pairs: "80,96" clicks once there. A dialog cannot be tested without
 * something pressing a button in it, and TOSEMU_KEYS only presses keys.
 */
static void clicks_from_environment(void)
{
    static int done;
    const char *clicks = getenv("TOSEMU_CLICKS");
    int x, y, n;

    if (done || !clicks)
        return;

    done = 1;

    while (sscanf(clicks, "%d,%d%n", &x, &y, &n) == 2)
    {
        if (w.click_count + 2 > (int)(sizeof w.clicks / sizeof w.clicks[0]))
            break;

        w.clicks[w.click_count].buttons = 1;    /* down */
        w.clicks[w.click_count].x = (int16_t)x;
        w.clicks[w.click_count].y = (int16_t)y;
        w.click_count++;

        w.clicks[w.click_count].buttons = 0;    /* and up again */
        w.clicks[w.click_count].x = (int16_t)x;
        w.clicks[w.click_count].y = (int16_t)y;
        w.click_count++;

        w.mouse_x = (int16_t)x;
        w.mouse_y = (int16_t)y;

        clicks += n;
        if (*clicks == ' ' || *clicks == ',')
            clicks++;
    }
}

int gfx_button_take(int16_t *buttons, int16_t *x, int16_t *y)
{
    int i;

    clicks_from_environment();

    if (w.click_count == 0)
        return 0;

    *buttons = w.clicks[0].buttons;
    *x = w.clicks[0].x;
    *y = w.clicks[0].y;

    for (i = 1; i < w.click_count; i++)
        w.clicks[i-1] = w.clicks[i];
    w.click_count--;

    return 1;
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
     * What the key means first, where it is second.
     *
     * The positional rule is right for the main block and wrong for anything
     * standing in for a key an ST had and this keyboard does not. Scroll Lock
     * is the case that decides the order: it sits at 0x46, inside the block,
     * so asking where it is would answer before anyone asked what it was for.
     *
     * A GEM application reads this half to tell keys apart. A menu shortcut is
     * a scan code rather than a letter, so leaving it empty makes every
     * shortcut in every application unreachable.
     */
    scan = scancode_for(sym);

    if (scan == 0 && key >= 1 && key < 0x54)
        scan = (uint16_t)key;

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

static struct window *window_of(struct wl_surface *surface)
{
    int i;

    for (i = 0; i < WINDOWS; i++)
        if (w.windows[i].used && w.windows[i].surface == surface)
            return &w.windows[i];

    return 0;
}

/*
 * Where the pointer is, in the screen's own pixels.
 *
 * A window shows a rectangle of the screen scaled up by a whole number, so
 * going back is dividing by the scale and adding where that rectangle starts.
 * Doing it here is what keeps every other part of the emulator from having to
 * know a window was involved at all - and it is why a dialog can be dragged
 * anywhere without the application noticing.
 */
static void pointer_at(struct window *win, wl_fixed_t x, wl_fixed_t y)
{
    if (!win)
        return;

    w.mouse_x = (int16_t)(win->sx + wl_fixed_to_int(x) / SCALE);
    w.mouse_y = (int16_t)(win->sy + wl_fixed_to_int(y) / SCALE);
}

static void pt_enter(void *data, struct wl_pointer *p, uint32_t serial,
                     struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
    (void)data; (void)p; (void)serial;

    w.pointer_in = window_of(s);
    pointer_at(w.pointer_in, x, y);
}

static void pt_leave(void *data, struct wl_pointer *p, uint32_t serial,
                     struct wl_surface *s)
{
    (void)data; (void)p; (void)serial; (void)s;

    w.pointer_in = 0;
}

static void pt_motion(void *data, struct wl_pointer *p, uint32_t time,
                      wl_fixed_t x, wl_fixed_t y)
{
    (void)data; (void)p; (void)time;

    pointer_at(w.pointer_in, x, y);
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

    if (w.click_count < (int)(sizeof w.clicks / sizeof w.clicks[0]))
    {
        w.clicks[w.click_count].buttons = w.buttons;
        w.clicks[w.click_count].x = w.mouse_x;
        w.clicks[w.click_count].y = w.mouse_y;
        w.click_count++;
    }
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

/* Windows *****************************************************************/

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
static void surface_configure(void *data, struct xdg_surface *s,
                              uint32_t serial)
{
    struct window *win = data;

    xdg_surface_ack_configure(s, serial);
    win->configured = 1;
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
     * noted and the picture stays the size it is.
     */
    (void)data; (void)t; (void)width; (void)height; (void)states;
}

static void toplevel_close(void *data, struct xdg_toplevel *t)
{
    struct window *win = data;

    (void)t;

    /*
     * Closing the screen is the machine being told to stop, and it stops.
     *
     * Closing a dialog is not something GEM has a way of saying. There is no
     * cancel key and no "the dialog was dismissed" - a dialog ends when one of
     * its own buttons is pressed, and nothing else ends it. What is sent
     * instead is Undo, which is the key GEM programs use for cancelling, so an
     * application that handles it gets what it expects. form_do does not, so a
     * dialog put up by form_do stays up.
     *
     * That is a button that does nothing on such a dialog, which is not good,
     * but the alternative is worse: the only other key that ends form_do is
     * Return, and Return presses the default button. Closing a window is not
     * a way anyone should be able to agree to something.
     */
    /*
     * There is no window here whose closing means the machine should stop -
     * the screen is not shown, and every window belongs to the application.
     *
     * A GEM window's closer sends the message an application gets when its own
     * close box is clicked, which is exactly what this is: the desktop's frame
     * standing in for the one GEM would have drawn.
     *
     * A dialog is different. GEM has no way of saying a dialog was closed:
     * there is no cancel key, and a dialog ends when one of its own buttons is
     * pressed. Undo is sent, being the key GEM programs use for cancelling, so
     * an application that listens for it gets what it expects.
     */
    if (win->handle > 0)
        host_window_closed(win->handle);
    else
        key_post(0x6100);   /* Undo */
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
    else if (!strcmp(interface, zxdg_decoration_manager_v1_interface.name))
        w.decorations = wl_registry_bind(registry, name,
                                         &zxdg_decoration_manager_v1_interface,
                                         1);
    else if (!strcmp(interface, xdg_wm_dialog_v1_interface.name))
        w.wm_dialog = wl_registry_bind(registry, name,
                                       &xdg_wm_dialog_v1_interface, 1);
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
static int window_buffer(struct window *win)
{
    struct wl_shm_pool *pool;
    int fd;

    win->bytes = (size_t)win->width * win->height * 4;

    fd = memfd_create("tosemu-window", MFD_CLOEXEC);
    if (fd < 0)
        return 0;

    if (ftruncate(fd, (off_t)win->bytes) < 0)
    {
        close(fd);
        return 0;
    }

    win->pixels = mmap(0, win->bytes, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (win->pixels == MAP_FAILED)
    {
        win->pixels = 0;
        close(fd);
        return 0;
    }

    pool = wl_shm_create_pool(w.shm, fd, (int32_t)win->bytes);
    win->buffer = wl_shm_pool_create_buffer(pool, 0, win->width, win->height,
                                            win->width * 4,
                                            WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    return win->buffer != 0;
}

/* Whichever GEM window is nearest the front, for a dialog to belong to */
static struct window *window_topmost(void)
{
    int i;

    for (i = 1; i < WINDOWS; i++)
        if (w.windows[i].used)
            return &w.windows[i];

    return 0;
}

static int window_create(struct window *win, const char *title,
                         struct surface *shows,
                         int16_t sx, int16_t sy, int16_t sw, int16_t sh,
                         struct window *parent)
{
    memset(win, 0, sizeof *win);

    win->shows = shows;
    win->sx = sx;
    win->sy = sy;
    win->sw = sw;
    win->sh = sh;
    win->width = sw * SCALE;
    win->height = sh * SCALE;

    win->surface = wl_compositor_create_surface(w.compositor);
    win->xdg_surface = xdg_wm_base_get_xdg_surface(w.wm_base, win->surface);
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);

    win->toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    xdg_toplevel_add_listener(win->toplevel, &toplevel_listener, win);
    xdg_toplevel_set_title(win->toplevel, title);
    xdg_toplevel_set_app_id(win->toplevel, "se.e8johan.tosemu");

    /*
     * Neither of these can be resized: the screen is the size the emulated
     * machine's screen is, and a dialog is the size the application made it.
     * Saying so both ways is what tells a desktop there is nothing for a
     * maximise button to do, so it does not offer one.
     */
    xdg_toplevel_set_min_size(win->toplevel, win->width, win->height);
    xdg_toplevel_set_max_size(win->toplevel, win->width, win->height);

    /*
     * Ask the desktop to put its own frame round it.
     *
     * Without this a compositor is entitled to assume the window draws its own,
     * and a window with no frame has nothing to take hold of: there is no title
     * bar to drag it by and no buttons to close it with. GEM draws frames round
     * its own windows, but these are not those - these are the windows GEM's
     * screen and its dialogs are shown in, and they belong to the desktop.
     */
    if (w.decorations)
    {
        win->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
            w.decorations, win->toplevel);
        zxdg_toplevel_decoration_v1_set_mode(
            win->decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    /*
     * A dialog says so, and says whose it is. That is what buys the behaviour
     * that makes it a dialog rather than merely look like one: kept above its
     * parent, the parent kept out of reach while it is up, and the decorations
     * a compositor gives a dialog rather than a document.
     *
     * None of it stops the window being moved, which is the point of doing it
     * this way round rather than by making the one window smaller.
     */
    if (parent)
    {
        xdg_toplevel_set_parent(win->toplevel, parent->toplevel);

        if (w.wm_dialog)
        {
            win->dialog = xdg_wm_dialog_v1_get_xdg_dialog(w.wm_dialog,
                                                          win->toplevel);
            xdg_dialog_v1_set_modal(win->dialog);
        }
    }

    wl_surface_commit(win->surface);
    wl_display_roundtrip(w.display);    /* Waits for the first configure */

    if (!window_buffer(win))
        return 0;

    win->used = 1;

    return 1;
}

static void window_destroy(struct window *win)
{
    if (!win->used)
        return;

    if (w.pointer_in == win)
        w.pointer_in = 0;

    if (win->pixels)
        munmap(win->pixels, win->bytes);
    if (win->buffer)
        wl_buffer_destroy(win->buffer);
    if (win->decoration)
        zxdg_toplevel_decoration_v1_destroy(win->decoration);
    if (win->dialog)
        xdg_dialog_v1_destroy(win->dialog);
    if (win->toplevel)
        xdg_toplevel_destroy(win->toplevel);
    if (win->xdg_surface)
        xdg_surface_destroy(win->xdg_surface);
    if (win->surface)
        wl_surface_destroy(win->surface);

    memset(win, 0, sizeof *win);
}

int gfx_open(struct surface *screen)
{
    memset(&w, 0, sizeof w);

    /*
     * A way to say no. The tests run GEM programs, and a test suite that opens
     * and closes windows on whoever's desktop happens to be logged in is a
     * nuisance rather than a feature.
     */
    if (getenv("TOSEMU_NO_WINDOW"))
        return 0;

    w.screen = screen;

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

    /*
     * No window is opened here. There is nothing to show until GEM opens
     * something, and the screen itself is never shown.
     */
    w.showing = 1;

    return 1;
}

void gfx_close()
{
    int i;

    for (i = 0; i < WINDOWS; i++)
        window_destroy(&w.windows[i]);

    if (w.xkb_state)
        xkb_state_unref(w.xkb_state);
    if (w.keymap)
        xkb_keymap_unref(w.keymap);
    if (w.xkb)
        xkb_context_unref(w.xkb);
    if (w.display)
        wl_display_disconnect(w.display);

    memset(&w, 0, sizeof w);
}

int gfx_showing()
{
    return w.showing && !w.closed;
}

void gfx_window_open(int16_t handle, const char *title, int16_t x, int16_t y,
                     int16_t sw, int16_t sh)
{
    if (!gfx_showing() || handle < 1 || handle >= WINDOWS)
        return;

    if (sw <= 0 || sh <= 0)
        return;

    gfx_window_close(handle);

    if (window_create(&w.windows[handle], title, w.screen, x, y, sw, sh, 0))
        w.windows[handle].handle = handle;
    else
        window_destroy(&w.windows[handle]);
}

void gfx_window_move(int16_t handle, int16_t x, int16_t y,
                     int16_t sw, int16_t sh)
{
    struct window *win;

    if (handle < 1 || handle >= WINDOWS)
        return;

    win = &w.windows[handle];
    if (!win->used)
        return;

    /*
     * Where a window is on the emulated screen is the AES's business and not
     * the desktop's, so moving one there does not move the window here: the
     * desktop decides where its windows sit, and moving one about is the
     * person's to do. What does change is which part of the screen is shown,
     * and how large the window has to be to show it.
     */
    if (sw == win->sw && sh == win->sh)
    {
        win->sx = x;
        win->sy = y;
        return;
    }

    /* A different size needs a different buffer, which is a new window */
    gfx_window_open(handle, "", x, y, sw, sh);
}

void gfx_window_title(int16_t handle, const char *title)
{
    if (handle < 1 || handle >= WINDOWS || !w.windows[handle].used)
        return;

    xdg_toplevel_set_title(w.windows[handle].toplevel, title);
}

void gfx_window_close(int16_t handle)
{
    if (handle < 1 || handle >= WINDOWS)
        return;

    window_destroy(&w.windows[handle]);
}

void gfx_dialog_open(struct surface *shows, int16_t x, int16_t y,
                     int16_t sw, int16_t sh)
{
    if (!gfx_showing())
        return;

    gfx_dialog_close();

    if (sw <= 0 || sh <= 0 || !shows)
        return;

    /*
     * A dialog belongs to whichever window is in front, if there is one. That
     * is what a desktop needs to know to keep it above that window and to keep
     * that window out of reach while it is up - and it is why a dialog is
     * usually kept out of the task list, being reached by way of the window it
     * belongs to rather than on its own.
     */
    if (!window_create(&w.windows[DIALOG], "Dialog", shows, x, y, sw, sh,
                       window_topmost()))
        window_destroy(&w.windows[DIALOG]);
}

void gfx_dialog_close()
{
    window_destroy(&w.windows[DIALOG]);
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

/*
 * Puts the part of the screen a window shows into it.
 *
 * A pixel at a time, through the palette and out to as many pixels as the
 * scale asks for. This is the whole of that rectangle every time rather than
 * the part that changed: at ST sizes it is a few hundred thousand writes, and
 * knowing what changed is worth having only once there is something to spend
 * the saving on.
 */
static void window_present(struct window *win)
{
    int x, y, sx, sy;

    if (!win->used || !win->configured)
        return;

    for (y = 0; y < win->sh; y++)
    {
        for (x = 0; x < win->sw; x++)
        {
            uint32_t argb = emuvdi_palette_argb(
                surface_pixel(win->shows, (uint16_t)(win->sx + x),
                              (uint16_t)(win->sy + y)));

            for (sy = 0; sy < SCALE; sy++)
            {
                uint32_t *row = win->pixels
                              + (size_t)(y*SCALE + sy) * win->width
                              + x*SCALE;

                for (sx = 0; sx < SCALE; sx++)
                    row[sx] = argb;
            }
        }
    }

    wl_surface_attach(win->surface, win->buffer, 0, 0);
    wl_surface_damage_buffer(win->surface, 0, 0, win->width, win->height);
    wl_surface_commit(win->surface);
}

void gfx_present()
{
    int i;

    if (!gfx_showing())
        return;

    for (i = 0; i < WINDOWS; i++)
        window_present(&w.windows[i]);

    wl_display_flush(w.display);
}
