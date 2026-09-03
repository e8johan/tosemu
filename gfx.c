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
 *
 * The file is in two halves, and NO_WAYLAND keeps the first. That half is the
 * queues of keys and clicks, and the input a test asks for on the command line
 * when there is nobody to press anything - none of which involves a compositor.
 * The second half is the windows, and a build without Wayland answers there
 * what this already answers when nobody is logged in. See the Makefile.
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
#include <poll.h>
#include <sys/mman.h>

#ifndef NO_WAYLAND
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-dialog-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#endif

#include "surface.h"
#include "screen.h"
#include "settings.h"
#include "emuvdi/emuvdi.h"

/* Told to the AES when a window's frame is used to close it, so that the
 * application is sent the message it would have got from its own close box */
void host_window_closed(int16_t handle);

/* And when the desktop has made a window a different size, which the AES turns
 * into the message its own size box would have sent. The size is what is shown
 * of the window, in the screen's own pixels. */
void host_window_resized(int16_t handle, int16_t w, int16_t h);

/* And when it says which window somebody is working in, which is what a GEM
 * title bar is drawn light or dark to show */
void host_window_activated(int16_t handle, int16_t active);

/*
 * A title bar drawn into a surface of a window's own, for a window that has
 * none of GEM's and is on a desktop that draws none of its own either.
 *
 * The AES draws it, because what it draws is a GEM title bar - the same font,
 * the same close box, the same light and dark for the window in front - and
 * because the code that draws one is the AES's. See host_frame_strip in
 * aesframe.c, which is also where the reasoning for it being GEM's rather than
 * an imitation of somebody's desktop is written down.
 */
void host_frame_strip(struct surface *into, int16_t width, const char *name,
                      int active, int closer);

/*
 * And the same for the menu bar, whose furniture goes beside it rather than
 * above it: a handle to drag it by and a size box past that, both one row tall,
 * where the titles have run out. A title bar above a strip that is one row tall
 * by definition would be a second strip saying the name of an application whose
 * name is already the first thing on the bar. See host_frame_handle.
 */
void host_frame_handle(struct surface *into, int16_t width, int16_t height,
                       int active);

/*
 * How much larger than an ST pixel one on the desktop is.
 *
 * A whole number, because anything else is a blur: an ST pixel becomes a
 * square block of them and stays a hard edge, which is what the artwork of the
 * period was drawn for. screen_scale reads it, because it is also what the
 * size of the display is divided by to arrive at a screen that fills it, and
 * those two have to be the same number.
 *
 * It is kept per window rather than once, because it is a property of how a
 * window is being shown rather than of the machine: two windows of the same
 * application can honestly be shown at different sizes, and one day they will
 * be.
 */

#ifndef NO_WAYLAND

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
    struct xdg_popup *popup;
    struct xdg_dialog_v1 *dialog;
    struct zxdg_toplevel_decoration_v1 *decoration;

    struct wl_buffer *buffer;
    uint32_t *pixels;
    size_t bytes;

    /* The part of the screen it shows, in the screen's own pixels */
    int16_t sx, sy, sw, sh;

    /* How much larger than an ST pixel one of this window's is */
    int scale;

    /* And how large that rectangle is once scaled, which is what the
     * compositor sees */
    int width, height;

    /*
     * The largest and smallest the desktop may make it, in the screen's own
     * pixels, or nought for a window that may not be resized at all.
     *
     * The largest is not a preference. A window is a rectangle of the screen
     * the AES lays out and that screen is only so big, so a drag that arrived
     * at a larger size would be asking for pixels there is no memory for.
     */
    int16_t min_w, min_h, max_w, max_h;

    /*
     * The size the compositor last asked for and the size the AES was last
     * told about, both in the screen's own pixels.
     *
     * They are two numbers because a drag asks on every frame and is answered
     * once, when it ends - see toplevel_configure, which says why.
     */
    int16_t asked_sw, asked_sh;
    int16_t told_sw, told_sh;

    /*
     * The size the window was before it was last resized here, and a sync
     * waiting for the compositor to have seen that resize.
     *
     * A window is resized here the moment the application sets its rectangle,
     * without waiting to be told to. The compositor knows nothing of that
     * until the commit reaches it, and anything it had already sent describes
     * the window as it was - so a configure can arrive saying the old size
     * when the window is no longer that size and nobody is asking for it to
     * be. Told to the AES it becomes WM_SIZED, and an application doing as it
     * is told undoes the resize it had just made.
     *
     * The sync is what says when that has stopped being possible. Everything
     * the compositor sends before it answers was decided before it saw the new
     * size; after it, what arrives is about the window as it now is.
     */
    struct wl_callback *settling;
    int16_t was_sw, was_sh;

    /* Whether a drag is running on it, which is what makes the window show the
     * outline of the size being chosen rather than what it is showing */
    int dragging;

    /* Whether the compositor said this window is the one being worked in, as
     * of the last configure */
    int active;

    /*
     * The frame this window draws for itself, and how tall it is in the
     * screen's own pixels - nought for a window that is not drawing one.
     *
     * A GEM window carries its own frame in what it is showing and wants
     * nothing here. A dialog and the menu bar have no frame of their own and
     * ask the desktop for one, and a desktop is entitled to say no: GNOME
     * draws no frames at all round a Wayland window, and expects every program
     * to draw its own. A window with a frame from nowhere has nothing to move
     * it by, nothing to close it with and nothing saying what it is, which is
     * not a window so much as a rectangle that appeared.
     *
     * So one is drawn - by the AES, into this surface, and shown as part of the
     * window beside what the window is showing. The window is that much larger
     * than the rectangle it shows and everything that works in buffer pixels
     * has to allow for it, which is what the two sizes are for: they are the
     * difference between the two, and both are nought whenever somebody else is
     * drawing the frame.
     *
     * Which of them a window uses says which frame it has. A strip across the
     * top is a title bar and is what everything but the menu bar gets;
     * frame_w is the menu bar's handle and size box, which go on the end of the
     * row rather than above it - see host_frame_handle.
     */
    struct surface *frame;
    int16_t frame_h;
    int16_t frame_w;

    /* Whether GEM's own frame is in what this window shows, which settles the
     * question before it is asked: such a window wants nothing from the
     * desktop and nothing drawn here, whatever a compositor later says about
     * frames in general */
    int own_frame;

    /* Whether that strip has a close box. Everything that can be closed has
     * one, and the menu bar cannot: a close box that did nothing would be a
     * trap rather than a decoration. */
    int frame_closer;

    /* What it says, kept because it is drawn again whenever the window becomes
     * the one in front or stops being it */
    char name[64];

    /* The compositor dismissed it, which only happens to menus */
    int gone;
};

/*
 * Slot 0 is the dialog, because there is one of those at a time. Then the GEM
 * windows, which the AES allows eight of and numbers from one, so their handle
 * is their slot. The menu bar and whichever menu is down get slots of their
 * own after those: the AES does not give them handles, and taking one of the
 * eight would be taking a window an application is entitled to.
 */
#define DIALOG   (0)
#define MENUBAR  (9)
#define MENU     (10)
#define WINDOWS  (11)

#endif /* NO_WAYLAND */

static struct {
#ifndef NO_WAYLAND
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

    /*
     * And whether it is on the strip that window drew for itself, with how far
     * along it in that window's own pixels.
     *
     * The strip is not part of the emulated screen. It is the frame, and a
     * press on it is for the frame to answer - which is why where the pointer
     * is on it is kept here rather than turned into a place on the screen, and
     * why the AES hears nothing about either.
     */
    int on_frame;
    int frame_x, frame_y;
#endif /* NO_WAYLAND */

    /*
     * The last thing the person did, as the compositor numbers them.
     *
     * Taking a grab has to be answering something they did - it is how a menu
     * is allowed to take over the pointer, and how a program is stopped from
     * taking it over whenever it likes.
     */
    uint32_t serial;

    struct surface *screen;

    /* Keys waiting to be read, oldest first */
    uint16_t keys[32];
    int key_count;

    int16_t mouse_x, mouse_y;   /* In the screen's pixels, not a window's */

    /*
     * What is held down as far as anything asking has been told, and what is
     * really held down this instant. They are not the same thing and must not
     * be: the second is what happened, the first is what has been looked at,
     * and a press that is both answers twice.
     */
    int16_t buttons;
    int16_t pressed;

    /*
     * Whether the pointer has ever been anywhere.
     *
     * Before it has, there is no answer to where it is - not a position of
     * nought, which is a real place, and one in the top left corner where the
     * menu bar's first title is. A menu opening by itself on the way up is
     * what that costs.
     */
    int mouse_known;

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
        int move;               /* Only the pointer moving, nothing pressed */
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
#ifndef NO_WAYLAND
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
#endif /* NO_WAYLAND */

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
    const char *keys = setting("TOSEMU_KEYS");
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

/*
 * Where the pointer is and what is held down, as of the last change anybody
 * took off the queue.
 *
 * That is what a wait wants. The event loop is answered by changes rather than
 * by states, and it considers each one where it happened, so the state it
 * reads has to be the state as of the change it was handed and not as of some
 * later one it has not looked at yet.
 */
void gfx_mouse(int16_t *x, int16_t *y, int16_t *buttons)
{
    clicks_from_environment();

    *x = w.mouse_x;
    *y = w.mouse_y;
    *buttons = w.buttons;
}

/*
 * And as of now, which is a different question and has a different answer.
 *
 * An application that polls takes nothing and waits for nothing: it asks,
 * looks, and asks again. Answering it as of the last change taken tells it
 * whatever the wait before its loop happened to leave behind, and that never
 * moves - so a person dragging a slider watches it sit still, and the loop
 * they are dragging in never ends, because the button it is waiting to see
 * released was read before they released it.
 *
 * On an ST there was nothing to arrange. The keyboard processor kept the
 * position and the buttons up to date and reading them cost nothing and
 * consumed nothing, so a program could poll them and did. This is that:
 * anything the compositor has said is listened to first, and the answer is as
 * of the newest thing in the queue rather than the oldest.
 *
 * The queue itself is left alone, because what is in it belongs to the waits.
 * They need every change and not merely the latest - that is what stops a
 * click too quick to be seen from reading as a button that was never pressed.
 */
void gfx_mouse_now(int16_t *x, int16_t *y, int16_t *buttons)
{
    int i;

    clicks_from_environment();

    /* An application that polls never reaches the event loop, so this is the
     * only place its idea of where the pointer is can catch up */
    gfx_dispatch_ready();

    *x = w.mouse_x;
    *y = w.mouse_y;
    *buttons = w.buttons;

    for (i = 0; i < w.click_count; i++)
    {
        *x = w.clicks[i].x;
        *y = w.clicks[i].y;

        /* A move says where the pointer went and nothing about the buttons */
        if (!w.clicks[i].move)
            *buttons = w.clicks[i].buttons;
    }
}

int gfx_mouse_known(void)
{
    clicks_from_environment();

    return w.mouse_known;
}

uint16_t gfx_kstate()
{
#ifdef NO_WAYLAND
    /* No keyboard, so nothing is being held down on it. Which is also the
     * answer on a machine that has one and nobody logged in to use it. */
    return 0;
#else
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
#endif
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
    const char *clicks = setting("TOSEMU_CLICKS");
    int x, y, n;

    if (done || !clicks)
        return;

    done = 1;

    while (*clicks)
    {
        int ux, uy, n2, moving = 0, held = 0;

        if (*clicks == '@')
        {
            moving = 1;
            clicks++;
        }

        /*
         * "!x,y" presses there and does not let go, which is what a person
         * does for the tenth of a second between pressing a mouse button and
         * releasing it. Nothing else here can express that - every other form
         * queues the release immediately - and a wait that answers wrongly
         * while a button is held is invisible until somebody uses a real one.
         */
        if (*clicks == '!')
        {
            held = 1;
            clicks++;
        }

        if (sscanf(clicks, "%d,%d%n", &x, &y, &n) != 2)
            break;

        ux = x; uy = y;

        if (w.click_count + 2 > (int)(sizeof w.clicks / sizeof w.clicks[0]))
            break;

        /*
         * "@x,y" only moves the pointer there. A menu cannot be worked
         * without it: GEM opens a menu when the pointer arrives among the
         * titles, so getting one open means moving rather than clicking.
         */
        if (moving)
        {
            w.clicks[w.click_count].buttons = 0;
            w.clicks[w.click_count].x = (int16_t)x;
            w.clicks[w.click_count].y = (int16_t)y;
            w.clicks[w.click_count].move = 1;
            w.click_count++;

            clicks += n;
            while (*clicks == ' ' || *clicks == ',')
                clicks++;
            continue;
        }

        /*
         * "x,y-x2,y2" presses at the first and releases at the second, the
         * pointer having moved in between. That is a drag, and a menu cannot
         * be worked without one: pulling a menu down is a press on the title,
         * a move to the entry and a release on it.
         */
        if (clicks[n] == '-'
            && sscanf(clicks + n + 1, "%d,%d%n", &ux, &uy, &n2) == 2)
            n += 1 + n2;

        w.clicks[w.click_count].buttons = 1;    /* down where it started */
        w.clicks[w.click_count].x = (int16_t)x;
        w.clicks[w.click_count].y = (int16_t)y;
        w.clicks[w.click_count].move = 0;
        w.click_count++;

        if (held)
        {
            clicks += n;
            while (*clicks == ' ' || *clicks == ',')
                clicks++;
            continue;
        }

        w.clicks[w.click_count].buttons = 0;    /* and up where it ended */
        w.clicks[w.click_count].x = (int16_t)ux;
        w.clicks[w.click_count].y = (int16_t)uy;
        w.clicks[w.click_count].move = 0;
        w.click_count++;

        clicks += n;
        while (*clicks == ' ' || *clicks == ',')
            clicks++;
    }
}

/*
 * Moves the pointer to the next place it was going, and says whether it went
 * anywhere. Whoever is waiting looks again after each one, because a wait for
 * the pointer to arrive somewhere is answered by where it is rather than by
 * anything having been pressed.
 */
int gfx_motion_take(void)
{
    int i;

    clicks_from_environment();

    if (w.click_count == 0 || !w.clicks[0].move)
        return 0;

    w.mouse_x = w.clicks[0].x;
    w.mouse_y = w.clicks[0].y;
    w.mouse_known = 1;

    for (i = 1; i < w.click_count; i++)
        w.clicks[i-1] = w.clicks[i];
    w.click_count--;

    return 1;
}

/*
 * What the next change is, without taking it.
 *
 * The AES has to look at a press before deciding whose it is - a press on a
 * window's frame is the AES's own and never reaches the application - and
 * looking is not the same as taking. Taking it and putting it back would be
 * the same thing written twice and wrong once.
 */
int gfx_button_peek(int16_t *buttons, int16_t *x, int16_t *y)
{
    clicks_from_environment();

    if (w.click_count == 0 || w.clicks[0].move)
        return 0;

    *buttons = w.clicks[0].buttons;
    *x = w.clicks[0].x;
    *y = w.clicks[0].y;

    return 1;
}

int gfx_button_take(int16_t *buttons, int16_t *x, int16_t *y)
{
    int i;

    clicks_from_environment();

    if (w.click_count == 0 || w.clicks[0].move)
        return 0;

    *buttons = w.clicks[0].buttons;
    *x = w.clicks[0].x;
    *y = w.clicks[0].y;

    /*
     * Taking a change moves the pointer to where it happened. Anything asking
     * afterwards how things are gets the answer as of the change it was just
     * handed, rather than as of some later one it has not seen yet.
     */
    w.mouse_x = *x;
    w.mouse_y = *y;
    w.buttons = *buttons;
    w.mouse_known = 1;

    for (i = 1; i < w.click_count; i++)
        w.clicks[i-1] = w.clicks[i];
    w.click_count--;

    return 1;
}

/* Everything below here talks to a compositor, and a build without Wayland
 * ends the file with the answers it would have arrived at without one */
#ifndef NO_WAYLAND

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
/*
 * The pointer is not ours any more, because it left or because the window it
 * was in has gone.
 *
 * Nothing is pressed any more, as far as we can tell. We only hear about the
 * button while the pointer is ours, and pressing something that closes the
 * window it was pressed in - a menu entry does that, and so does a dialog's
 * OK button - sends the release to whatever the pointer is over afterwards,
 * which is somebody else's window or the desktop. It never arrives, and a
 * button held down for ever answers every wait for a press: the AES stops
 * looking at where the pointer is and nothing responds again.
 *
 * So the button comes up. That is not a guess about what the person did - it
 * is the honest answer, which is that we no longer know and will not pretend
 * otherwise. If it really is still held, the enter that follows says so.
 */
static void pointer_gone(void)
{
    w.pointer_in = 0;
    w.on_frame = 0;

    if (!w.pressed && !w.buttons)
        return;

    w.pressed = 0;

    if (w.click_count < (int)(sizeof w.clicks / sizeof w.clicks[0]))
    {
        w.clicks[w.click_count].buttons = 0;
        w.clicks[w.click_count].x = w.mouse_x;
        w.clicks[w.click_count].y = w.mouse_y;
        w.clicks[w.click_count].move = 0;
        w.click_count++;
    }
}

static void pointer_at(struct window *win, wl_fixed_t x, wl_fixed_t y)
{
    int top, right;

    if (!win)
        return;

    /* Where what the window is showing begins and ends: under whatever strip
     * the window drew for itself, and short of whatever it drew beside it */
    top = win->frame_h * win->scale;
    right = win->sw * win->scale;

    w.frame_x = wl_fixed_to_int(x);
    w.frame_y = wl_fixed_to_int(y);

    /*
     * What a window draws for itself is not a place on the emulated screen.
     *
     * Told as one it would come out as the first row of what the window shows,
     * or the last column of it, and both of those are places with things on
     * them that can be clicked: the top of a dialog, or the right hand end of
     * the menu bar. So the pointer being on the frame is written down as being
     * there, the AES is told nothing, and where it is on the screen stays what
     * it last was - which is the honest answer, the pointer not being on the
     * screen at all.
     */
    w.on_frame = w.frame_y < top
              || (win->frame_w && w.frame_x >= right);

    if (w.on_frame)
        return;

    w.mouse_x = (int16_t)(win->sx + w.frame_x / win->scale);
    w.mouse_y = (int16_t)(win->sy + (w.frame_y - top) / win->scale);
    w.mouse_known = 1;
}

static void pt_enter(void *data, struct wl_pointer *p, uint32_t serial,
                     struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
    (void)data; (void)p;

    w.serial = serial;
    w.pointer_in = window_of(s);
    pointer_at(w.pointer_in, x, y);
}

static void pt_leave(void *data, struct wl_pointer *p, uint32_t serial,
                     struct wl_surface *s)
{
    (void)data; (void)p; (void)serial;

    /*
     * Only if it is the window the pointer is actually in.
     *
     * Leaving one window and entering another is two messages, and they do not
     * have to arrive in that order - a menu closing under the pointer gets the
     * new window first and the old one's leave afterwards. Forgetting where
     * the pointer is because a window it had already left says so leaves us
     * ignoring every move it makes after that, which looks exactly like
     * everything having stopped.
     */
    if (w.pointer_in != window_of(s))
        return;

    pointer_gone();
}

static void pt_motion(void *data, struct wl_pointer *p, uint32_t time,
                      wl_fixed_t x, wl_fixed_t y)
{
    (void)data; (void)p; (void)time;

    pointer_at(w.pointer_in, x, y);
}

/* What a press on a window's own title bar is worth, which is written out
 * below: it needs what the desktop's close box does, and that is further down
 * than this */
static void frame_press(struct window *win, int16_t buttons);

static void pt_button(void *data, struct wl_pointer *p, uint32_t serial,
                      uint32_t time, uint32_t button, uint32_t state)
{
    int16_t bit;

    (void)data; (void)p; (void)time;

    w.serial = serial;

    /* GEM numbers them from the left, and has two */
    switch (button)
    {
        case 0x110: bit = 1; break;     /* BTN_LEFT */
        case 0x111: bit = 2; break;     /* BTN_RIGHT */
        default: return;
    }

    /*
     * A press on the strip a window drew for itself belongs to the frame and
     * not to the application. It is where the desktop's own title bar would
     * have been, and what a title bar does - move the window, close it, put up
     * the desktop's menu for it - is not a thing GEM is told about.
     *
     * The release is not waited for. Everything the strip does takes the
     * pointer away from us the moment it starts: a drag and the window menu
     * are the compositor's from the press onwards, and a close takes the window
     * itself away.
     */
    if (w.on_frame && w.pointer_in)
    {
        if (state == WL_POINTER_BUTTON_STATE_PRESSED)
            frame_press(w.pointer_in, bit);

        return;
    }

    /*
     * Queued, and not written into the state as well.
     *
     * The queue is what happened and the state is what has been looked at, and
     * one press must not be both. Setting the state here too let a single
     * press answer twice: once from the state, by a wait that never got as far
     * as draining anything, and once more from the change still sitting in the
     * queue behind it - which walked the file selector two folders down for
     * every click.
     *
     * So this only says what happened. Whoever takes it is what makes it so.
     */
    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        w.pressed |= bit;
    else
        w.pressed &= ~bit;

    if (w.click_count < (int)(sizeof w.clicks / sizeof w.clicks[0]))
    {
        w.clicks[w.click_count].buttons = w.pressed;
        w.clicks[w.click_count].x = w.mouse_x;
        w.clicks[w.click_count].y = w.mouse_y;
        w.clicks[w.click_count].move = 0;
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

/* The two ways a window can be shown, and the one that is not the ordinary
 * one - both written out below, and both wanted here */
static void window_present(struct window *win);
static int window_drag_preview(struct window *win, int width, int height);
static int window_rebuffer(struct window *win, int width, int height,
                           uint32_t format);
static int window_resize(struct window *win, int16_t sw, int16_t sh);

/*
 * Whether the frames the desktop is not drawing are drawn here instead.
 *
 * This is the same setting aeswind.c reads, and it is read here for the other
 * half of the question. There it decides whether GEM draws a window's own title
 * bar; here it decides who frames the windows that have no title bar of GEM's -
 * a dialog, the menu bar, a window created without one.
 *
 * Unset, the desktop is asked and answers. Saying gem is saying not to ask at
 * all, which is for a desktop whose frames are not wanted rather than one that
 * has none to give - the answer is the same either way, and this is how to have
 * it without waiting to be refused.
 */
static const char *decorations_wanted(void)
{
    static const char *said;
    static int asked;

    if (!asked)
    {
        said = setting("TOSEMU_DECORATIONS");
        asked = 1;
    }

    return said;
}

static int frames_are_ours(void)
{
    const char *said = decorations_wanted();

    return said && strcmp(said, "gem") == 0;
}

/* And the other way about: the desktop's frames asked for outright, which is
 * what decides the one window that would otherwise never ask for them */
static int frames_are_the_desktops(void)
{
    const char *said = decorations_wanted();

    return said && strcmp(said, "desktop") == 0;
}

/* How large the box round a character is, which is what every strip of a
 * window's frame is measured in: a title bar is one tall and a close box is
 * one of each */
static void frame_sizes(int16_t *wbox, int16_t *hbox)
{
    int16_t handle, wchar, hchar;

    emuvdi_graf_handle(&handle, &wchar, &hchar, wbox, hbox);
}

/* What the strip says, drawn again: the name is not the only thing on it that
 * changes, the bar itself being light or dark for whether this is the window
 * somebody is working in */
static void window_frame_paint(struct window *win)
{
    if (!win->frame)
        return;

    if (win->frame_w)
        host_frame_handle(win->frame, win->frame_w, win->sh, win->active);
    else
        host_frame_strip(win->frame, win->sw, win->name, win->active,
                         win->frame_closer);
}

/*
 * Somewhere for the frame to be drawn, in the screen's own planes so that it is
 * magnified and coloured like everything else the window shows.
 *
 * A title bar is as wide as the window and as tall as one row; the menu bar's
 * handle is as tall as the bar and as wide as it needs to be. Made again
 * whenever the window changes in the direction the frame follows, there being
 * nothing to be done with a title bar that is not as wide as its window.
 */
static void window_frame_make(struct window *win)
{
    int16_t wide, tall;

    if (win->frame)
    {
        surface_free(win->frame);
        win->frame = 0;
    }

    if (!w.screen || win->sw <= 0)
        return;

    wide = win->frame_w ? win->frame_w : win->sw;
    tall = win->frame_w ? win->sh : win->frame_h;

    if (wide <= 0 || tall <= 0)
        return;

    /*
     * A whole number of words across, whatever the window's width is.
     *
     * The VDI works out how long a row is by rounding the width down to a word
     * and surface_create rounds the allocation up, so a surface that is not a
     * multiple of sixteen pixels wide is one where the two disagree - and every
     * row the VDI draws lands a word further along than the row before it. An
     * Atari screen was always a multiple of sixteen and the question never came
     * up; a title bar is as wide as whatever window it is on, and a dialog is
     * whatever width the application made it.
     *
     * What is in it is still drawn to the width asked for. The few pixels past
     * the end of that are never shown, the window being exactly as wide as it
     * says it is.
     */
    win->frame = surface_create((uint16_t)((wide + 15) & ~15), (uint16_t)tall,
                                surface_planes(w.screen));

    /* No room for one. The window then has no frame at all, which is what it
     * had before this was tried, rather than a gap where one was going to be */
    if (!win->frame)
    {
        win->frame_h = 0;
        win->frame_w = 0;
        return;
    }

    window_frame_paint(win);
}

/*
 * How large the desktop may make the window, in the pixels the compositor
 * counts in.
 *
 * A window that has said nothing about being resizable is pinned to the size it
 * is, which is what a window without a size box is: GEM gave no way to resize
 * one. A window that has said otherwise is held between the two sizes only the
 * AES knows - see gfx_window_limits.
 *
 * Both have to allow for the strip the window draws for itself, which is why
 * this is one place rather than the two it was said in. A window told it may
 * not be taller than what it shows is a window whose frame does not fit in it.
 */
static void window_sizes(struct window *win)
{
    int16_t least_w, least_h, most_w, most_h;

    if (!win->toplevel)
        return;

    if (win->max_w > 0 && win->max_h > 0)
    {
        least_w = win->min_w;
        least_h = win->min_h;
        most_w = win->max_w;
        most_h = win->max_h;
    }
    else
    {
        least_w = most_w = win->sw;
        least_h = most_h = win->sh;
    }

    xdg_toplevel_set_min_size(win->toplevel,
                              (least_w + win->frame_w) * win->scale,
                              (least_h + win->frame_h) * win->scale);
    xdg_toplevel_set_max_size(win->toplevel,
                              (most_w + win->frame_w) * win->scale,
                              (most_h + win->frame_h) * win->scale);
}

/*
 * Whether this window is to draw its own frame, which is not always known when
 * the window is made.
 *
 * Asking the desktop for a frame is asking: the answer arrives as an event, and
 * a compositor is entitled to answer that it draws none - which is what GNOME
 * answers, and what any compositor with no decoration manager at all is saying
 * by not having one. It may also change its mind later, a person having
 * switched the desktop's frames off, and there is nothing else this could be
 * told by.
 *
 * So the answer arrives here whenever it arrives. Before the window is up there
 * is nothing to do but write it down, the size and the buffer being worked out
 * from it in a moment; afterwards the window has to change size, because a
 * frame that appears takes room the window does not have and one that goes
 * leaves a strip of nothing where it was.
 */
static void window_frame_needed(struct window *win, int needed)
{
    int16_t wbox, hbox;

    if (needed == (win->frame_h != 0 || win->frame_w != 0))
        return;

    if (needed)
    {
        frame_sizes(&wbox, &hbox);

        /*
         * The menu bar gets a handle and a size box on the end of its row and
         * everything else gets a title bar across the top.
         *
         * Two character cells for the handle and one for the size box, which
         * is what a close box and a full box are given at the other end of a
         * window's title bar - and enough to take hold of at any scale, being
         * measured in the same characters the bar itself is.
         */
        if (win == &w.windows[MENUBAR])
            win->frame_w = (int16_t)(wbox * 3);
        else
            win->frame_h = hbox;
    }
    else
    {
        win->frame_h = 0;
        win->frame_w = 0;
    }

    if (!win->used)
        return;

    window_frame_make(win);
    window_sizes(win);
    window_rebuffer(win, (win->sw + win->frame_w) * win->scale,
                    (win->sh + win->frame_h) * win->scale,
                    WL_SHM_FORMAT_XRGB8888);
}

/*
 * What the desktop answered when it was asked to draw the frame, which is not
 * always what it was asked for.
 *
 * A compositor that does not draw frames says so here, and saying so is the
 * whole reason for listening: without it the request would be made, quietly
 * refused, and the window would appear with no frame from anybody.
 */
static void decoration_configure(void *data,
                                 struct zxdg_toplevel_decoration_v1 *d,
                                 uint32_t mode)
{
    struct window *win = data;

    (void)d;

    /* A window that carries GEM's own frame asked for none of the desktop's
     * and is not listening for an answer about one */
    if (win->own_frame)
        return;

    window_frame_needed(win,
                        mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
    decoration_configure
};

/*
 * The compositor saying how large a window is to be, which is how a window
 * comes to be resized: the person takes hold of the size box, the compositor
 * runs the drag and says the size it arrived at, over and over as it goes.
 *
 * Nothing is resized here. What the AES is told is the size in the screen's
 * own pixels, and what it does with that is send the application the message
 * its own size box would have sent - because the application is what decides
 * whether the new shape suits what it is showing, exactly as it did on an ST.
 * The window here becomes that size when the application says so, which is why
 * an application that ignores the message keeps the window it had.
 *
 * A size of nought means the compositor has no opinion, which is what the
 * first configure of a window's life usually says. So does a window that is
 * not up yet: the first configure arrives inside window_create, before there
 * is a handle to tell the AES about.
 */
static void toplevel_configure(void *data, struct xdg_toplevel *t,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    struct window *win = data;
    int16_t sw, sh;
    uint32_t *state;
    int active = 0;
    int dragging = 0;
    int maximized = 0;

    (void)t;

    if (!win->used)
        return;

    /*
     * What the compositor says about the window besides how large it is, both
     * of which arrive as states rather than as events of their own.
     *
     * Which window somebody is working in matters because a window draws its
     * own title bar: with nothing of the desktop's round the outside, that bar
     * is the only thing saying which window is in front. Whether a drag is
     * running matters for the size, below, and so does whether the window is
     * maximised - see window_maximize, which is what asks for that.
     */
    wl_array_for_each(state, states)
    {
        if (*state == XDG_TOPLEVEL_STATE_ACTIVATED)
            active = 1;
        if (*state == XDG_TOPLEVEL_STATE_RESIZING)
            dragging = 1;
        if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
            maximized = 1;
    }

    if (active != win->active)
    {
        win->active = active;

        /* A window drawing its own title bar draws it again, that bar being
         * where light or dark says which window is in front */
        if (win->frame)
        {
            window_frame_paint(win);
            window_present(win);
        }

        if (win->handle >= 1)
            host_window_activated(win->handle, (int16_t)active);
    }

    /*
     * The menu bar answers for itself.
     *
     * Nothing above here lays it out. The AES draws the bar across the whole
     * emulated screen and knows nothing of the window, so a width somebody has
     * dragged to is a width this decides, and what it decides is how much of
     * the bar is shown - the titles are at the left hand end and what is being
     * trimmed away is the empty part after them.
     *
     * It is done on every frame of the drag rather than shown as a rubber band
     * and answered when the button comes up. That is what the rubber band is
     * for on a window, where the answer costs the emulated machine a redraw of
     * its document; here there is no application in it at all and the whole of
     * the work is a few thousand writes into a buffer.
     */
    if (win == &w.windows[MENUBAR])
    {
        int16_t shown = (int16_t)(width / win->scale - win->frame_w);

        if (width > 0 && shown > 0 && shown != win->sw)
            window_resize(win, shown, win->sh);

        return;
    }

    /*
     * Everything below is about how large the window is to be, and the dialog
     * takes no part in it. It is the size the application made it, it is pinned
     * to that size, and there is nobody to tell about a size anyway: the AES
     * gives it no handle.
     */
    if (win->handle < 1)
        return;

    /* Back into the screen's pixels, which is the only size anything above
     * here deals in. Rounding down rather than up: a window one pixel short of
     * what was asked for is a gap at the edge, and one pixel over is a row of
     * the screen that does not exist. What the window draws for itself comes
     * off first, that strip being the window's rather than the screen's. */
    if (width > 0 && height > 0 && !maximized)
    {
        int16_t asked_sw = (int16_t)(width / win->scale);
        int16_t asked_sh = (int16_t)(height / win->scale - win->frame_h);

        /*
         * Unless it is the size the window has just stopped being, said by a
         * compositor that had not yet seen it change - see the settling field.
         * That is not asking for anything and is not written down either: kept
         * here it would be handed on the next time a configure arrives without
         * a size of its own.
         */
        if (!win->settling
            || asked_sw != win->was_sw || asked_sh != win->was_sh)
        {
            win->asked_sw = asked_sw;
            win->asked_sh = asked_sh;
        }
    }

    /*
     * The application hears nothing at all while a drag is running.
     *
     * A compositor says how large the window is to be on every frame of one,
     * and acting on each of those means the application redrawing its document
     * dozens of times a second on a 68000 - which is slow enough to feel, and
     * to make the drag itself lag behind the pointer. It is also more than was
     * ever asked for: what an ST did was drag an outline and tell the
     * application once, when the button came up, and an application built for
     * that redraws once.
     *
     * So the size is noted, the last one noted is the one that counts, and what
     * the window shows in the meantime is that outline - the window really is
     * the size being chosen, and what is in it is a rubber band with the
     * desktop showing through. Drawing that costs nothing the emulated machine
     * pays for: it is a few thousand writes into a buffer, and the 68000 is not
     * involved in any of it.
     */
    if (width > 0 && height > 0 && dragging)
    {
        if (!win->dragging || width != win->width || height != win->height)
            window_drag_preview(win, width, height);
        return;
    }

    if (dragging)
        return;

    /*
     * The drag has ended, so the outline goes and the window shows what it is
     * showing again.
     *
     * Its buffer stays the size the drag arrived at rather than going back to
     * the size the window was, because going back is a jump to the old size and
     * then another to the new one - the application has not answered yet, and
     * will not until it has been told. So the picture is put in the buffer as
     * it stands, with the screen's background where it does not reach, and the
     * moment it is asked for the rest is drawn.
     */
    if (win->dragging)
    {
        win->dragging = 0;
        window_present(win);
    }

    /*
     * A maximised window is told nothing about how large it is, because the
     * size that arrives with the maximising is the display's and the window is
     * the emulated screen's.
     *
     * The work area is the display less whatever the desktop keeps for itself,
     * and the screen the AES lays windows out on is worked out from the whole
     * display. Neither is reliably the larger: a desktop with a panel on it
     * leaves a work area shorter than a window as large as the AES allows, and
     * one without leaves it longer. Either way the number is about the display
     * and answering it is the application resizing the window to something
     * that is no longer as large as it goes - which takes the maximising off
     * again, and the window jumps out to the corner and straight back.
     *
     * There is nothing that needs saying anyway. Being maximised is a thing
     * that happened because the window was already as large as the AES allows,
     * and it is still that size: what was wanted from the compositor was the
     * corner and not the rectangle.
     */
    if (maximized)
        return;

    sw = win->asked_sw;
    sh = win->asked_sh;

    if (sw <= 0 || sh <= 0)
        return;

    if (sw == win->sw && sh == win->sh)
        return;

    /*
     * And not twice for the same answer. An application is entitled to ignore
     * WM_SIZED and keep the window it had, and a compositor repeats the size
     * it wants in every configure - including the ones that are really about
     * something else, like the window being clicked on. Without this, such an
     * application would be asked again on every one of those, for ever.
     */
    if (sw == win->told_sw && sh == win->told_sh)
        return;

    win->told_sw = sw;
    win->told_sh = sh;

    host_window_resized(win->handle, sw, sh);
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

/*
 * A press on the title bar a window drew for itself.
 *
 * That bar is the desktop's frame in everything but who drew it, so it does
 * what the desktop's would have done: the close box closes the window, the
 * other button puts up the desktop's own menu for it - which is where
 * minimising lives, GEM having no gadget for something it cannot do - and
 * anywhere else drags the window.
 *
 * The dragging and the menu are asked for rather than done. A client is not
 * told where its windows are and cannot move one, so what a title bar does with
 * a press is hand it back to the compositor and let the compositor run the
 * drag, which is what GEM's own title bar does on a window that has one. See
 * gfx_window_drag_move.
 *
 * Closing is the exception and is done here, because closing is not the
 * desktop's idea of it: GEM's close box asks the application, which is why an
 * application can ask whether you meant it. toplevel_close is what the
 * desktop's own close button arrives at, and this is the same press.
 */
static void frame_press(struct window *win, int16_t buttons)
{
    int16_t wbox, hbox;

    frame_sizes(&wbox, &hbox);

    if (buttons == 1 && !win->frame_w && win->frame_closer
        && w.frame_x < wbox * win->scale)
    {
        toplevel_close(win, win->toplevel);
        return;
    }

    /* Both of the rest have to be answering something the person did, and the
     * serial of that is what proves it. There is nothing to answer when the
     * input was made up rather than done, which is what happens under a test. */
    if (!win->toplevel || !w.seat || !w.serial)
        return;

    if (buttons == 2)
        xdg_toplevel_show_window_menu(win->toplevel, w.seat, w.serial,
                                      w.frame_x, w.frame_y);
    /*
     * The size box on the end of the menu bar, which is at the very edge of it.
     *
     * It runs the desktop's own resize drag, the same way a GEM window's size
     * box does, and from the right hand edge alone: what the drag changes is
     * how much of the bar is shown, and how tall a menu bar is is not a matter
     * of taste. The window is held to one height either way - see the limits
     * the AES gives it - so a drag from the corner would only be a drag from
     * the edge with a corner drawn on it.
     */
    else if (win->frame_w
             && w.frame_x >= (win->sw + win->frame_w - wbox) * win->scale)
        xdg_toplevel_resize(win->toplevel, w.seat, w.serial,
                            XDG_TOPLEVEL_RESIZE_EDGE_RIGHT);
    else
        xdg_toplevel_move(win->toplevel, w.seat, w.serial);

    wl_display_flush(w.display);
}

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

/*
 * Memory both this and the compositor can see, which is how a picture is
 * handed over without copying it through the socket.
 *
 * The format is a choice because a window is two things at different moments.
 * Showing part of the screen it is a picture with nothing behind it, and
 * XRGB says so. Showing the outline of a resize it is mostly not there at
 * all - what a rubber band is, is the desktop seen through it - and that needs
 * the alpha byte to mean something.
 */
static int window_buffer(struct window *win, uint32_t format)
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
                                            win->width * 4, format);
    wl_shm_pool_destroy(pool);
    close(fd);

    return win->buffer != 0;
}

/*
 * A window whose buffer has to be a different size, which is a different
 * buffer.
 *
 * The window itself stays. Tearing it down and opening another one at the new
 * size would work and is what this used to do, but not while somebody is
 * dragging a corner: the drag belongs to the window it started on, and taking
 * that window away in the middle of one ends it. This is a few hundred
 * kilobytes of memory changing hands and nothing the desktop has to hear about.
 *
 * The old buffer is let go of after the new one has been shown rather than
 * before, because a buffer the compositor is still displaying is not ours to
 * take away, and destroying it first leaves a moment with nothing in the
 * window.
 */
static int window_rebuffer(struct window *win, int width, int height,
                           uint32_t format)
{
    struct wl_buffer *was = win->buffer;
    uint32_t *had = win->pixels;
    size_t held = win->bytes;
    int old_width = win->width, old_height = win->height;

    win->width = width;
    win->height = height;

    if (!window_buffer(win, format))
    {
        win->width = old_width;
        win->height = old_height;
        win->buffer = was;
        win->pixels = had;
        win->bytes = held;
        return 0;
    }

    window_present(win);

    if (had)
        munmap(had, held);
    if (was)
        wl_buffer_destroy(was);

    return 1;
}

/* The compositor has answered the sync, so everything it sends from here on
 * was decided knowing how large the window now is - see the settling field */
static void window_settled(void *data, struct wl_callback *callback,
                           uint32_t stamp)
{
    struct window *win = data;

    (void)stamp;

    if (win->settling == callback)
        win->settling = 0;

    wl_callback_destroy(callback);
}

static const struct wl_callback_listener settled_listener = {
    window_settled
};

/* A window showing a different rectangle of the screen, which is the ordinary
 * case: the buffer is that rectangle scaled up and the picture goes in it */
static int window_resize(struct window *win, int16_t sw, int16_t sh)
{
    int16_t old_sw = win->sw, old_sh = win->sh;

    win->sw = sw;
    win->sh = sh;
    win->dragging = 0;

    /* A frame follows the window in the direction it lies along: a title bar
     * is as wide as the window it is on, and the menu bar's handle is as tall
     * as the bar */
    if ((win->frame_h && sw != old_sw) || (win->frame_w && sh != old_sh))
        window_frame_make(win);

    if (!window_rebuffer(win, (sw + win->frame_w) * win->scale,
                         (sh + win->frame_h) * win->scale,
                         WL_SHM_FORMAT_XRGB8888))
    {
        win->sw = old_sw;
        win->sh = old_sh;

        window_frame_make(win);

        return 0;
    }

    /*
     * The new size is committed, so ask the compositor to say when it has seen
     * it. Until it answers, a configure saying the window is the size it just
     * stopped being is one it decided before the commit arrived and not a
     * request for that size back.
     *
     * The sync goes after the commit rather than before, because what is being
     * waited for is the commit having been dealt with. Any sync still in
     * flight from an earlier resize is dropped: it would answer for a size
     * that has since been left behind, and the one that matters is the last.
     */
    win->was_sw = old_sw;
    win->was_sh = old_sh;

    if (win->settling)
        wl_callback_destroy(win->settling);

    win->settling = wl_display_sync(w.display);
    wl_callback_add_listener(win->settling, &settled_listener, win);

    return 1;
}

/*
 * And the same window while somebody is dragging its corner.
 *
 * The buffer becomes the size the drag has arrived at, so the window really is
 * that size and a person can see what they are choosing. What goes in it is not
 * the window: it is the outline of one, with the desktop showing through, which
 * is the rubber band an ST drew while a window was being sized. What the window
 * is showing has not changed and will not until the application says so - see
 * toplevel_configure, which says why the application is left out of the drag.
 */
static int window_drag_preview(struct window *win, int width, int height)
{
    win->dragging = 1;

    return window_rebuffer(win, width, height, WL_SHM_FORMAT_ARGB8888);
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

static void popup_configure(void *data, struct xdg_popup *popup,
                            int32_t x, int32_t y, int32_t width, int32_t height)
{
    (void)data; (void)popup; (void)x; (void)y; (void)width; (void)height;
}

/* The compositor taking the menu away, which it does when something is clicked
 * outside it. The AES is watching the pointer itself and will come to the same
 * conclusion; this is only the window going. */
static void popup_done(void *data, struct xdg_popup *popup)
{
    struct window *win = data;

    (void)popup;

    win->gone = 1;

    /*
     * The compositor took the menu away, which is what it does when something
     * outside it is clicked. The AES has no idea: the click was never ours to
     * see, which is the whole reason for the grab.
     *
     * So it is told the only way it understands - that the pointer is nowhere
     * near the menu and that a button went down there. That is what a click
     * outside a menu is, and the AES does with it what it would have done:
     * puts the menu away and reports that nothing was chosen.
     */
    if (w.screen)
    {
        int16_t x = (int16_t)(surface_width(w.screen) - 1);
        int16_t y = (int16_t)(surface_height(w.screen) - 1);
        int i;

        w.mouse_x = x;
        w.mouse_y = y;
        w.mouse_known = 1;

        for (i = 0; i < 2; i++)
        {
            if (w.click_count >= (int)(sizeof w.clicks / sizeof w.clicks[0]))
                break;

            w.clicks[w.click_count].buttons = (i == 0) ? 1 : 0;
            w.clicks[w.click_count].x = x;
            w.clicks[w.click_count].y = y;
            w.clicks[w.click_count].move = 0;
            w.click_count++;
        }
    }
}

static const struct xdg_popup_listener popup_listener = {
    popup_configure,
    popup_done,
};

static int window_create(struct window *win, const char *title,
                         struct surface *shows,
                         int16_t sx, int16_t sy, int16_t sw, int16_t sh,
                         struct window *parent, int own_frame)
{
    uint32_t want;

    memset(win, 0, sizeof *win);

    win->own_frame = own_frame;
    win->shows = shows;
    win->sx = sx;
    win->sy = sy;
    win->sw = sw;
    win->sh = sh;
    win->scale = screen_scale();
    win->width = sw * win->scale;
    win->height = sh * win->scale;

    /* What a title bar of its own would say, if it turns out to need one */
    if (title)
    {
        strncpy(win->name, title, sizeof win->name - 1);
        win->name[sizeof win->name - 1] = 0;
    }

    /* Everything but the menu bar, which is not a window anybody can close: a
     * close box that did nothing would be a trap */
    win->frame_closer = win != &w.windows[MENUBAR];

    win->surface = wl_compositor_create_surface(w.compositor);
    win->xdg_surface = xdg_wm_base_get_xdg_surface(w.wm_base, win->surface);
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);

    win->toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    xdg_toplevel_add_listener(win->toplevel, &toplevel_listener, win);
    xdg_toplevel_set_title(win->toplevel, title);
    xdg_toplevel_set_app_id(win->toplevel, "se.e8johan.tosemu");

    /*
     * A window that has not said otherwise cannot be resized, which is the
     * honest answer for most of them: a dialog is the size the application
     * made it, and the menu bar is the width of the screen and one box tall.
     * Saying the same number both ways is what tells a desktop there is
     * nothing for a maximise button to do, so it does not offer one.
     *
     * A GEM window with a size box says otherwise the moment it is open - see
     * gfx_window_limits, which is the AES handing over the two sizes only it
     * knows: the smallest the frame will fit in, and what is left of the
     * screen it lays windows out on.
     */
    window_sizes(win);

    /*
     * Whose frame goes round it.
     *
     * A window that draws its own says so, and gets nothing from the desktop:
     * no title bar, no buttons, no border. That is what a GEM window wants,
     * because GEM draws all of those itself and two sets is a title bar inside
     * a title bar - and it is worth having rather than merely tidy, because
     * GEM's close box means "ask the application" where a desktop's means
     * "take the window away".
     *
     * Everything else asks the desktop for one. A dialog, the menu bar and a
     * window created without a title strip draw no frame of their own, and a
     * window with no frame at all has nothing to take hold of: nothing to drag
     * it by and nothing to close it with. Saying so is not a formality either -
     * without it a compositor is entitled to assume the window draws its own.
     *
     * Asking is not getting. A compositor may answer that it draws no frames,
     * which is what GNOME answers and what a compositor with no decoration
     * manager at all has already said by not having one, and then the frame is
     * drawn here - see window_frame_needed, which is where that answer arrives.
     * A window with no manager to ask, or one told not to bother asking, knows
     * without asking.
     *
     * The menu bar never asks. What a desktop would put round it is a title bar
     * above a strip that is one row tall, saying the name of an application
     * whose name is already the first word on the bar - and the bar would then
     * be the size the desktop's frame made it rather than the size of what is
     * on it. So it draws the smallest frame that lets a bar be moved and
     * resized, which is a handle and a size box on the end of the row. Asking
     * for the desktop's frames outright is still asking, and that setting still
     * wins.
     */
    if (own_frame)
        want = ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
    else if (frames_are_ours() || !w.decorations
             || (win == &w.windows[MENUBAR] && !frames_are_the_desktops()))
    {
        want = ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        window_frame_needed(win, 1);
    }
    else
        want = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;

    if (w.decorations)
    {
        win->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
            w.decorations, win->toplevel);
        zxdg_toplevel_decoration_v1_add_listener(win->decoration,
                                                 &decoration_listener, win);
        zxdg_toplevel_decoration_v1_set_mode(win->decoration, want);
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

    /* Waits for the first configure, and with it for whatever the desktop has
     * to say about the frame */
    wl_display_roundtrip(w.display);

    /*
     * Which is settled now, however it was settled, so the window is as tall as
     * what it shows and whatever it is drawing above that.
     *
     * The sizes are said a second time for the same reason. They were said out
     * of the size the window was going to be, and a window that has since
     * gained a strip is one whose largest size no longer has room for its own
     * frame - which a compositor is entitled to take literally and refuse to
     * make it that big.
     */
    window_frame_make(win);
    win->width = (sw + win->frame_w) * win->scale;
    win->height = (sh + win->frame_h) * win->scale;
    window_sizes(win);

    if (!window_buffer(win, WL_SHM_FORMAT_XRGB8888))
        return 0;

    win->used = 1;

    return 1;
}

static void window_destroy(struct window *win)
{
    if (!win->used)
        return;

    /* Taking a window away under the pointer is the pointer leaving it, and
     * the compositor has no reason to say so: the window it would have named
     * is the one that has gone. */
    if (w.pointer_in == win)
        pointer_gone();

    /* Before the window goes, because the answer would be delivered to a
     * listener holding a pointer to it */
    if (win->settling)
        wl_callback_destroy(win->settling);

    if (win->pixels)
        munmap(win->pixels, win->bytes);
    if (win->buffer)
        wl_buffer_destroy(win->buffer);
    if (win->frame)
        surface_free(win->frame);
    if (win->decoration)
        zxdg_toplevel_decoration_v1_destroy(win->decoration);
    if (win->dialog)
        xdg_dialog_v1_destroy(win->dialog);
    if (win->popup)
        xdg_popup_destroy(win->popup);
    if (win->toplevel)
        xdg_toplevel_destroy(win->toplevel);
    if (win->xdg_surface)
        xdg_surface_destroy(win->xdg_surface);
    if (win->surface)
        wl_surface_destroy(win->surface);

    memset(win, 0, sizeof *win);
}

/*
 * Lets go of a connection that arrived by being forked, rather than closing
 * one this process opened.
 *
 * A child of fork has a copy of every file descriptor and a copy of the
 * library's idea of what is on the other end of them - the same window, the
 * same buffers, the same object identifiers. It must not tear any of that down,
 * because the objects belong to the parent and destroying them there would take
 * the parent's window away. Nor may it use them: two processes taking turns to
 * read one connection get half a message each.
 *
 * So the descriptor is closed and everything else forgotten. What the child
 * opens afterwards, if it opens anything, is its own.
 */
void gfx_forget(void)
{
    if (w.display)
        close(wl_display_get_fd(w.display));

    memset(&w, 0, sizeof w);
}

/*
 * Why there will be no windows.
 *
 * Failing to connect used to be answered with silence, on the grounds that a
 * machine with nobody logged in is the ordinary case and says nothing worth
 * hearing. On a desktop it is not ordinary at all, and the silence is the worst
 * part of it: the emulator starts, the application runs, the screen is drawn -
 * into memory, where nobody can see it - and nothing that happens afterwards
 * explains why no window ever appeared.
 *
 * An X session is the case worth naming outright. tosemu shows its windows on
 * Wayland and only on Wayland, so a person logged into Xorg, which is still
 * what some machines are given by default, gets a program that looks like it
 * started and then stopped. Neither is a fault to fix here - one is how the
 * emulator has always run without a compositor - so what this does is say
 * which of them happened.
 *
 * The two names read here are the session's own rather than tosemu's, which is
 * why they are read directly instead of through settings.c. Nobody sets either
 * of them to tell this program anything; they are how a program finds out what
 * kind of session it is in.
 */
static void say_why_there_is_no_window(void)
{
    if (getenv("WAYLAND_DISPLAY"))
        printf("GFX: there is a Wayland session here but connecting to it "
               "failed, so the screen stays in memory\n");
    else if (getenv("DISPLAY"))
        printf("GFX: this is an X session, and tosemu shows its windows on "
               "Wayland, so the screen stays in memory\n");
    else
        printf("GFX: there is no desktop session here, so the screen stays in "
               "memory\n");
}

int gfx_open(struct surface *screen)
{
    memset(&w, 0, sizeof w);

    /*
     * A way to say no. The tests run GEM programs, and a test suite that opens
     * and closes windows on whoever's desktop happens to be logged in is a
     * nuisance rather than a feature.
     */
    if (setting_flag("TOSEMU_NO_WINDOW"))
        return 0;

    w.screen = screen;

    w.display = wl_display_connect(0);
    if (!w.display)
    {
        say_why_there_is_no_window();
        return 0;
    }

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
                     int16_t sw, int16_t sh, int own_frame)
{
    if (!gfx_showing() || handle < 1 || handle >= WINDOWS)
        return;

    if (sw <= 0 || sh <= 0)
        return;

    gfx_window_close(handle);

    if (window_create(&w.windows[handle], title, w.screen, x, y, sw, sh, 0,
                      own_frame))
        w.windows[handle].handle = handle;
    else
        window_destroy(&w.windows[handle]);
}

/*
 * A window as large as the desktop area, said to the compositor.
 *
 * This is the one place that decides where a window sits on the desktop rather
 * than leaving it to the person, and it is the full box that earns the
 * exception. A window made as large as it goes and left wherever it happened
 * to be hangs off the bottom and the right of the display, which is not what
 * anybody clicking that box was asking for - it is the one GEM gadget whose
 * whole meaning is about position as well as size.
 *
 * Maximising is how that is asked for, because it is the only way of asking
 * for a position that Wayland has. There is no request that moves a window: a
 * client is not told where its windows are and cannot put them anywhere, and
 * the nearest thing is to say the window is maximised and let the desktop put
 * it in the corner it puts maximised windows in.
 *
 * The size that comes back of it is not used, and must not be. A compositor
 * maximises to the whole of the work area and takes no notice of the largest
 * size the window said it could be - it answers with the display's rectangle,
 * which is larger than the screen the AES lays windows out on, and the window
 * is that screen's and not the display's. So the corner is what is taken from
 * this and the size is still the AES's. The configure is answered like any
 * other, and host_window_resized cuts it down to the desktop area before the
 * application is told anything.
 */
static void window_maximize(struct window *win, int maximized)
{
    if (!win->toplevel)
        return;

    /*
     * Said every time rather than only when it changes, and nothing here
     * remembers which way the window was left.
     *
     * A record of that cannot be kept honestly. What the compositor last said
     * is behind: a request and the configure answering it cross in flight, so
     * the state at the moment a rectangle is set is not the state the window
     * is about to be in, and a full box pressed twice quickly would find the
     * old answer and do nothing. What was last asked for is no better, because
     * the desktop's own window menu maximises a window too and nothing here is
     * asked about it.
     *
     * Asking for the state a window is already in costs a configure saying so,
     * and this is reached only when an application sets a window's rectangle.
     */
    if (maximized)
        xdg_toplevel_set_maximized(win->toplevel);
    else
        xdg_toplevel_unset_maximized(win->toplevel);
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
    win->sx = x;
    win->sy = y;

    /*
     * Except when it has been made as large as the desktop area, which is the
     * full box however it was arrived at. There is no message saying the full
     * box was used - the application answers WM_FULLED by setting a rectangle
     * like any other - so the rectangle is what it is recognised by, and an
     * application setting that one by hand meant the same thing anyway.
     *
     * The size is the whole of the test. A window that large fits nowhere else
     * on the screen the AES lays windows out on, and host_window_resized keeps
     * every window inside it, so being that size and being in the corner are
     * the same thing. A window with no size box has no largest size, and there
     * is nothing for the full box to do to one.
     */
    window_maximize(win, win->max_w > 0 && win->max_h > 0
                         && sw >= win->max_w && sh >= win->max_h);

    if (sw == win->sw && sh == win->sh)
        return;

    if (sw <= 0 || sh <= 0)
        return;

    if (!window_resize(win, sw, sh))
        window_destroy(win);
}

/*
 * The two sizes only the AES knows, in the screen's own pixels: the smallest a
 * window's frame will fit in and the largest the screen leaves room for.
 *
 * Saying them is what makes a window resizable at all. Until this is called a
 * window is pinned to the size it opened at, which is right for one without a
 * size box - GEM had no way for such a window to be resized and neither should
 * a desktop.
 *
 * The largest is a real limit rather than a preference. A window is a
 * rectangle of the screen the AES lays out, and a drag arriving at a size
 * larger than that screen is asking for pixels there is no memory for.
 */
void gfx_window_limits(int16_t handle, int16_t min_w, int16_t min_h,
                       int16_t max_w, int16_t max_h)
{
    struct window *win;

    if (handle < 1 || handle >= WINDOWS)
        return;

    win = &w.windows[handle];
    if (!win->used || !win->toplevel)
        return;

    win->min_w = min_w;
    win->min_h = min_h;
    win->max_w = max_w;
    win->max_h = max_h;

    window_sizes(win);

    wl_surface_commit(win->surface);
}

/*
 * Taking hold of a window by its size box, which is the desktop resizing it.
 *
 * On an ST the AES drew a rubber band and worked out the new size when the
 * button came up. Here the window is a window of the desktop's, and a desktop
 * resizes one by running a drag of its own - so the size box asks for exactly
 * the drag the person would have got by taking hold of a corner, and what
 * comes back is a configure saying how large the window has become.
 *
 * It has to be answering something the person did, and the serial of that is
 * what proves it. There is nothing to answer when the input was made up rather
 * than done, which is what happens under a test.
 */
void gfx_window_drag_size(int16_t handle)
{
    struct window *win;

    if (handle < 1 || handle >= WINDOWS)
        return;

    win = &w.windows[handle];
    if (!win->used || !win->toplevel || !w.seat || !w.serial)
        return;

    xdg_toplevel_resize(win->toplevel, w.seat, w.serial,
                        XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT);
    wl_display_flush(w.display);
}

/*
 * And by its title bar, which is the desktop moving it.
 *
 * Nothing here or anywhere above it learns where the window ends up. Where a
 * window is on the desktop is the desktop's business - a client is not allowed
 * to know it, let alone choose it - and the AES has a coordinate space of its
 * own that does not change, which is what lets an application go on getting
 * answers that agree with each other while somebody drags its window about.
 */
void gfx_window_drag_move(int16_t handle)
{
    struct window *win;

    if (handle < 1 || handle >= WINDOWS)
        return;

    win = &w.windows[handle];
    if (!win->used || !win->toplevel || !w.seat || !w.serial)
        return;

    xdg_toplevel_move(win->toplevel, w.seat, w.serial);
    wl_display_flush(w.display);
}

/*
 * The desktop's own menu for a window - minimise, move, close, whatever this
 * desktop puts in one.
 *
 * It is here because minimising is not a thing GEM has. A window went away or
 * it did not; there was nowhere for one to go and so no gadget for sending it
 * there, which leaves nothing to draw in the title bar and nothing obvious to
 * click. What a person on this desktop already does for the things a title bar
 * has no button for is press the other mouse button on it, and this is that.
 *
 * The point is said in the screen's pixels, being where the press was, and has
 * to be handed over in the window's own.
 */
void gfx_window_menu(int16_t handle, int16_t x, int16_t y)
{
    struct window *win;

    if (handle < 1 || handle >= WINDOWS)
        return;

    win = &w.windows[handle];
    if (!win->used || !win->toplevel || !w.seat || !w.serial)
        return;

    xdg_toplevel_show_window_menu(win->toplevel, w.seat, w.serial,
                                  (x - win->sx) * win->scale,
                                  (y - win->sy + win->frame_h) * win->scale);
    wl_display_flush(w.display);
}

void gfx_window_title(int16_t handle, const char *title)
{
    struct window *win;

    if (handle < 1 || handle >= WINDOWS || !w.windows[handle].used)
        return;

    win = &w.windows[handle];

    xdg_toplevel_set_title(win->toplevel, title);

    /* And on the title bar the window draws for itself, where the name is the
     * whole of what it is for */
    if (title)
    {
        strncpy(win->name, title, sizeof win->name - 1);
        win->name[sizeof win->name - 1] = 0;
    }
    else
        win->name[0] = 0;

    window_frame_paint(win);
}

void gfx_window_close(int16_t handle)
{
    if (handle < 1 || handle >= WINDOWS)
        return;

    window_destroy(&w.windows[handle]);
}

/*
 * A menu that has dropped down.
 *
 * It is a popup rather than a window: it belongs to the bar, has no frame and
 * nothing to drag it by, does not appear in the window list, and goes away
 * when it is done with. That is what a menu is on any desktop, and saying so
 * gets all of it from the compositor rather than drawing an imitation.
 *
 * Where it goes is said relative to the bar, because a client is not allowed
 * to know where its own windows are, let alone put one somewhere. That is no
 * loss here: the menu belongs directly under its title, and where that is on
 * the bar is exactly what we do know.
 */
void gfx_menu_open(struct surface *shows, int16_t x, int16_t y,
                   int16_t sw, int16_t sh)
{
    struct window *win = &w.windows[MENU];
    struct window *bar = &w.windows[MENUBAR];
    struct xdg_positioner *where;

    if (!w.display || !bar->used)
        return;

    gfx_menu_close();

    memset(win, 0, sizeof *win);

    /* The screen when there is no surface of the menu's own, which is what
     * being unable to make one comes to: the menu is drawn where it always
     * was, and shows through the windows, which is worse than the alternative
     * only in that somebody notices */
    win->shows = shows ? shows : w.screen;
    win->sx = x;
    win->sy = y;
    win->sw = sw;
    win->sh = sh;

    /* At whatever the bar is shown at: the menu is part of the same bar, and
     * where it goes is said in the bar's own pixels */
    win->scale = bar->scale;
    win->width = sw * win->scale;
    win->height = sh * win->scale;

    win->surface = wl_compositor_create_surface(w.compositor);
    win->xdg_surface = xdg_wm_base_get_xdg_surface(w.wm_base, win->surface);
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);

    /*
     * The rectangle it hangs off, in the bar's pixels: the point on the bar
     * directly above where the menu goes. Anchoring the menu's top left corner
     * there puts it under its title, and lets the compositor slide it along if
     * it would otherwise run off the edge of the screen.
     *
     * Whatever title bar the bar is drawing for itself is above all of that and
     * counts, the point being said in the bar window's pixels rather than in
     * the screen's.
     */
    where = xdg_wm_base_create_positioner(w.wm_base);
    xdg_positioner_set_size(where, win->width, win->height);
    xdg_positioner_set_anchor_rect(where, (x - bar->sx) * bar->scale,
                                   (y - bar->sy + bar->frame_h) * bar->scale,
                                   1, 1);
    xdg_positioner_set_anchor(where, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(where, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_constraint_adjustment(where,
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X
        | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);

    win->popup = xdg_surface_get_popup(win->xdg_surface, bar->xdg_surface,
                                       where);
    xdg_positioner_destroy(where);

    xdg_popup_add_listener(win->popup, &popup_listener, win);

    /*
     * Take the pointer and the keyboard for as long as the menu is down.
     *
     * Without this a click next to the menu goes to whatever is underneath -
     * another program, or the desktop - and we are never told it happened, so
     * the menu stays down for ever with no way to dismiss it. The compositor
     * tells us instead, by taking the menu away and saying so.
     *
     * A grab has to be answering something the person did, and there is
     * nothing to answer when the input was made up rather than done, which is
     * what happens under a test. Asking anyway would have the menu dismissed
     * the moment it appeared.
     */
    if (w.seat && w.serial)
        xdg_popup_grab(win->popup, w.seat, w.serial);

    wl_surface_commit(win->surface);
    wl_display_roundtrip(w.display);

    win->used = 1;

    if (!window_buffer(win, WL_SHM_FORMAT_XRGB8888))
    {
        window_destroy(win);
        return;
    }

    window_present(win);
}

void gfx_menu_close(void)
{
    window_destroy(&w.windows[MENU]);
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
                       window_topmost(), 0))
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
    {
        int why = wl_display_get_error(w.display);

        /*
         * Saying which, because the two are nothing alike: a window closed is
         * the person finishing, and a protocol error is this program having
         * done something a compositor will not stand for. Both look identical
         * from here - the connection stops - and only one of them is a bug.
         */
        if (why)
            printf("The compositor rejected something we asked for: %s\n",
                   strerror(why));

        w.closed = 1;
    }
}

void gfx_flush()
{
    if (w.display)
        wl_display_flush(w.display);
}

/*
 * The same, for whatever the compositor has said already.
 *
 * gfx_dispatch waits when there is nothing to read, which is right for an
 * event loop and wrong for a caller that is only catching up - so whether
 * there is anything is asked first, and nothing said means nothing done.
 */
void gfx_dispatch_ready(void)
{
    struct pollfd waiting;

    if (!w.display)
        return;

    wl_display_dispatch_pending(w.display);
    wl_display_flush(w.display);

    waiting.fd = wl_display_get_fd(w.display);
    waiting.events = POLLIN;
    waiting.revents = 0;

    if (poll(&waiting, 1, 0) > 0)
        gfx_dispatch();
}

/*
 * Puts the part of the screen a window shows into it.
 *
 * A pixel at a time, through the palette and out to as many pixels as the
 * scale asks for. This is the whole of that rectangle every time rather than
 * the part that changed: at ST sizes it is a few hundred thousand writes, and
 * knowing what changed is worth having only once there is something to spend
 * the saving on.
 *
 * The buffer is not always the size of that rectangle. A drag leaves the window
 * the size the drag ended at, and what it shows only becomes that size when the
 * application takes the new rectangle - so anything the picture does not reach
 * is filled with the colour the screen's background is, and anything past the
 * end of the buffer is left off. Neither lasts longer than the moment between
 * the drag ending and the application answering it, and an application that
 * never answers is honestly showing what it kept.
 */
/*
 * One rectangle of a surface into the buffer, magnified, starting at a corner
 * of it.
 *
 * Everything a window shows goes through here: what the window is showing, and
 * whatever frame the window is drawing for itself - a title bar above it, or
 * the menu bar's handle beside it. Where it starts is the whole of the
 * difference between them.
 */
static void window_magnify(struct window *win, struct surface *from,
                           int16_t fx, int16_t fy, int16_t fw, int16_t fh,
                           int left, int top)
{
    int rows = top + fh * win->scale;
    int columns = left + fw * win->scale;
    int x, y, sx, sy;

    if (rows > win->height)
        rows = win->height;
    if (columns > win->width)
        columns = win->width;

    for (y = 0; y < fh; y++)
    {
        if (top + y * win->scale >= rows)
            break;

        for (x = 0; x < fw; x++)
        {
            uint32_t argb;

            if (left + x * win->scale >= columns)
                break;

            argb = emuvdi_palette_argb(
                surface_pixel(from, (uint16_t)(fx + x), (uint16_t)(fy + y)));

            /* The two tests above ask where a magnified pixel begins, and it
             * is scale pixels wide and scale pixels tall. The buffer's size is
             * whatever the compositor last sent and owes nothing to the scale,
             * so it can end part of the way through one of them - and then the
             * rest of that pixel is written past where the buffer stops. On
             * the last row that is past the end of the mapping, which is a
             * fault rather than a smear. So each of them is drawn as far as
             * the buffer reaches and no further. */
            for (sy = 0; sy < win->scale && top + y*win->scale + sy < rows; sy++)
            {
                uint32_t *row = win->pixels
                              + (size_t)(top + y*win->scale + sy) * win->width
                              + left + x*win->scale;

                for (sx = 0;
                     sx < win->scale && left + x*win->scale + sx < columns;
                     sx++)
                    row[sx] = argb;
            }
        }
    }
}

static void window_picture(struct window *win)
{
    uint32_t behind = emuvdi_palette_argb(0);

    /* Where what the window shows begins, which is under its own title bar
     * when it has one */
    int top = win->frame_h * win->scale;

    /* And where what it shows ends, which is where the menu bar's handle
     * begins when it is that window */
    int right = win->sw * win->scale;

    int rows = top + win->sh * win->scale;
    int columns = right + win->frame_w * win->scale;
    int x, y;

    if (rows > win->height)
        rows = win->height;
    if (columns > win->width)
        columns = win->width;

    for (y = 0; y < win->height; y++)
    {
        uint32_t *row = win->pixels + (size_t)y * win->width;

        if (y >= rows)
        {
            for (x = 0; x < win->width; x++)
                row[x] = behind;
            continue;
        }

        for (x = columns; x < win->width; x++)
            row[x] = behind;
    }

    if (win->frame && win->frame_w)
        window_magnify(win, win->frame, 0, 0, win->frame_w, win->sh, right, 0);
    else if (win->frame)
        window_magnify(win, win->frame, 0, 0, win->sw, win->frame_h, 0, 0);

    window_magnify(win, win->shows, win->sx, win->sy, win->sw, win->sh, 0, top);
}

/*
 * And the rubber band, which is what a window shows while its corner is being
 * dragged.
 *
 * An outline of the size the drag has arrived at and nothing else: the desktop
 * shows through the middle of it, which is what a rubber band is. GEM drew the
 * same thing on an ST while a window was being sized, and drew it exactly this
 * way - a rectangle in a line style of alternate pixels, which comes out as a
 * chequer of black and white and is legible against anything. See gsx_xline in
 * EmuTOS, where the two patterns are 0x5555 and 0xaaaa chosen by the parity of
 * the row: between them they light every pixel whose coordinates add up to an
 * odd number, and this is that said directly.
 *
 * The chequer is worked out in the screen's pixels rather than the desktop's,
 * so it stays a chequer of ST pixels however far the window is magnified.
 *
 * Its two colours are black and white said outright rather than taken from the
 * machine's palette, because this is the emulator drawing and not the machine:
 * an application that had set its first two colours to two greens would get a
 * rubber band nobody could see, and there is nothing on the emulated screen for
 * one to match anyway. Black against white is legible on any desktop.
 */
static void window_outline(struct window *win)
{
    uint32_t black = 0xff000000u;
    uint32_t white = 0xffffffffu;

    int thick = win->scale;
    int x, y;

    for (y = 0; y < win->height; y++)
    {
        uint32_t *row = win->pixels + (size_t)y * win->width;
        int edge_row = y < thick || y >= win->height - thick;

        for (x = 0; x < win->width; x++)
        {
            if (!edge_row && x >= thick && x < win->width - thick)
            {
                row[x] = 0;             /* nothing at all, so the desktop
                                         * shows through */
                continue;
            }

            row[x] = ((x / win->scale + y / win->scale) & 1) ? black : white;
        }
    }
}

static void window_present(struct window *win)
{
    if (!win->used || !win->configured || !win->pixels)
        return;

    if (win->dragging)
        window_outline(win);
    else
        window_picture(win);

    wl_surface_attach(win->surface, win->buffer, 0, 0);
    wl_surface_damage_buffer(win->surface, 0, 0, win->width, win->height);
    wl_surface_commit(win->surface);
}

void gfx_present()
{
    int i;

    if (!gfx_showing())
        return;

    /* A menu the compositor has taken away - which is what it does when
     * something outside it is clicked - is one we should stop showing. The AES
     * is watching the pointer itself and will come to the same conclusion in
     * its own time; this is only the window going. */
    if (w.windows[MENU].gone)
        gfx_menu_close();

    for (i = 0; i < WINDOWS; i++)
        window_present(&w.windows[i]);

    wl_display_flush(w.display);
}

#else /* NO_WAYLAND */

/*
 * The same half, built where there is no Wayland to build it against.
 *
 * None of this is a special case for a test to run into. The emulator already
 * has to work when there is nothing to connect to - gfx_open returns 0 on a
 * machine where nobody is logged in, and everything else here is written to do
 * nothing when it does - so what these answer is what the ordinary build
 * answers on a machine with no desktop, and the emulator takes the same path
 * through the AES either way.
 *
 * What is lost is being able to see it. The screen is in memory, which is
 * where GEM draws and where a screenshot is taken from, and that is the part
 * an application can observe.
 */

int gfx_open(struct surface *screen)
{
    (void)screen;

    memset(&w, 0, sizeof w);

    return 0;
}

void gfx_close()
{
    memset(&w, 0, sizeof w);
}

void gfx_forget(void)
{
    memset(&w, 0, sizeof w);
}

int gfx_showing()
{
    return 0;
}

void gfx_window_open(int16_t handle, const char *title, int16_t x, int16_t y,
                     int16_t sw, int16_t sh, int own_frame)
{
    (void)handle; (void)title; (void)x; (void)y; (void)sw; (void)sh;
    (void)own_frame;
}

void gfx_window_move(int16_t handle, int16_t x, int16_t y,
                     int16_t sw, int16_t sh)
{
    (void)handle; (void)x; (void)y; (void)sw; (void)sh;
}

void gfx_window_title(int16_t handle, const char *title)
{
    (void)handle; (void)title;
}

void gfx_window_close(int16_t handle)
{
    (void)handle;
}

void gfx_window_limits(int16_t handle, int16_t min_w, int16_t min_h,
                       int16_t max_w, int16_t max_h)
{
    (void)handle; (void)min_w; (void)min_h; (void)max_w; (void)max_h;
}

/*
 * The three a window asks the desktop to do for it.
 *
 * There being no desktop, none of them happens, which is what the ordinary
 * build also arrives at: each of these returns before it does anything unless
 * there is a toplevel and a serial to answer, and a machine with nobody logged
 * in has neither. A resize the desktop never runs is a resize the application
 * is never told about, and the window stays the size it was.
 */
void gfx_window_drag_size(int16_t handle)
{
    (void)handle;
}

void gfx_window_drag_move(int16_t handle)
{
    (void)handle;
}

void gfx_window_menu(int16_t handle, int16_t x, int16_t y)
{
    (void)handle; (void)x; (void)y;
}

void gfx_menu_open(struct surface *shows, int16_t x, int16_t y,
                   int16_t sw, int16_t sh)
{
    (void)shows; (void)x; (void)y; (void)sw; (void)sh;
}

void gfx_menu_close(void)
{
}

void gfx_dialog_open(struct surface *shows, int16_t x, int16_t y,
                     int16_t sw, int16_t sh)
{
    (void)shows; (void)x; (void)y; (void)sw; (void)sh;
}

void gfx_dialog_close()
{
}

/* Nothing to wait on beside the timer, which is what -1 says */
int gfx_fd()
{
    return -1;
}

void gfx_dispatch()
{
}

void gfx_dispatch_ready(void)
{
}

void gfx_flush()
{
}

void gfx_present()
{
}

#endif /* NO_WAYLAND */
