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
 * The menu bar.
 *
 * A GEM menu bar is a strip along the top of the screen belonging to whichever
 * application is in front, and it is an object tree like any other: the
 * application hands one over and the AES draws it, watches the pointer, drops
 * the menus down and says what was chosen.
 *
 * It gets a window of its own here, the way a dialog does, because the screen
 * itself is not shown. That window is the strip and nothing else, so the menu
 * bar follows its application about the desktop rather than sitting at the top
 * of a picture of an ST.
 *
 * A menu that drops down gets a surface of its own, hanging off the bar: no
 * frame, nothing to drag it by, not a window as far as the desktop is
 * concerned, and gone again when the menu is. That is what a menu is on any
 * desktop, and it is the AES that says when one appears - see the note in
 * emuvdi/gemmnlib.c, which is where the rectangle is known.
 */

#include "aes_p.h"

#include <string.h>

#include "gem_p.h"
#include "gfx.h"
#include "emuvdi/emuvdi.h"
#include "tossystem.h"
#include "m68k.h"

/* What menu_bar is asked to do, http://toshyp.atari.org/en/005005.html */
#define MENU_REMOVE  (0)
#define MENU_INSTALL (1)

/* The message an application is sent when something is chosen from a menu */
#define MN_SELECTED (10)

/* The bits of an object's state these three calls are each about, obdefs.h */
#define CHECKED  (0x0004)
#define DISABLED (0x0008)
#define SELECTED (0x0001)

/*
 * The window the bar is shown in.
 *
 * The AES gives handles to windows an application asked for, and it did not
 * ask for this one, so it is not one of the eight it is entitled to - it is a
 * slot of the desktop's own, past the end of them.
 */
#define BAR_WINDOW (9)

/* Where the tree came from, so that what the AES changed in it - a ticked
 * entry, a greyed out one - goes back to the application */
static uint32_t bar_tree;
static int bar_shown;

/*
 * Whether the menu is being run right now.
 *
 * Running it means waiting for the mouse, and waiting for the mouse is the
 * same loop that decides a press in the bar belongs to the menu - so without
 * this the first press would start the menu, and the menu's own wait would
 * see that press and start it again.
 */
static int bar_running;

/* Whether the pointer was already among the titles last time anyone looked,
 * so that arriving there counts once rather than every round */
static int was_among_titles;

void aes_menu_reset()
{
    bar_tree = 0;
    bar_shown = 0;
    bar_running = 0;
    was_among_titles = 0;
}

/* Whether there is a bar at all, which decides whether clicks have to be
 * looked at before the application sees them */
int aes_menu_shown()
{
    return bar_shown && !bar_running;
}

/*
 * Whether the pointer has just arrived among the titles, which is what starts
 * a menu.
 *
 * Hovering rather than clicking is how GEM menus work and always did: the
 * control manager waits for the pointer to enter the rectangle the titles are
 * in and runs the menu when it does, with the button still up. The menu then
 * waits for a press of its own to decide what was chosen, which is why opening
 * it with a press does not work - the press it is waiting for would already
 * have happened.
 *
 * Only the arrival counts. Sitting in the bar afterwards is not a reason to
 * start the menu again.
 */
static int was_among_titles;

int aes_menu_arrived(int16_t x, int16_t y)
{
    int16_t rx, ry, rw, rh;
    int inside;

    if (!bar_shown || bar_running || !gfx_mouse_known())
    {
        was_among_titles = 0;
        return 0;
    }

    emuvdi_menu_active(&rx, &ry, &rw, &rh);

    inside = (x >= rx) && (x < rx + rw) && (y >= ry) && (y < ry + rh);

    if (inside && !was_among_titles)
    {
        was_among_titles = 1;
        return 1;
    }

    if (!inside)
        was_among_titles = 0;

    return 0;
}

/*
 * Runs the menu, a press having landed in the bar.
 *
 * The tree goes across again first, because the application may have ticked
 * or greyed something since it was last drawn, and the menu that drops down
 * has to show what it says now.
 */
void aes_menu_click()
{
    int16_t title = 0, item = 0;
    int16_t message[8];
    void *host;
    int i;

    if (!bar_shown || !bar_tree)
        return;

    host = aes_tree_in(bar_tree);
    if (!host)
        return;

    bar_running = 1;

    emuvdi_menu_bar(host, 1);

    if (emuvdi_menu_do(&title, &item))
    {
        for (i = 0; i < 8; i++)
            message[i] = 0;

        message[0] = MN_SELECTED;
        message[3] = title;
        message[4] = item;

        aes_message_post(message);
    }

    bar_running = 0;
    was_among_titles = 1;      /* still there; do not start again on the spot */

    aes_tree_out();
    aes_tree_done();

    /*
     * The bar has changed - the title that was held open is not held open any
     * more - and the application is still inside the wait it was in when the
     * menu started, so nothing else is going to show it.
     */
    gem_present();
}

/* menu_bar ****************************************************************/

uint32_t AES_menu_bar()
{
    uint32_t address = aes_addrin(0);
    int16_t what = aes_intin(0);
    void *host;

    FUNC_TRACE_ENTER_ARGS {
        printf("    tree: 0x%x, %s\n", address,
               what == MENU_INSTALL ? "install" : "remove");
    }

    if (!gem_start())
        return AES_ERROR;

    switch (what)
    {
        case MENU_INSTALL:
            host = aes_tree_in(address);
            if (!host)
                return AES_ERROR;

            emuvdi_menu_bar(host, 1);

            bar_tree = address;
            bar_shown = 1;

            gfx_window_open(BAR_WINDOW, "Menu", 0, 0, emuvdi_screen_width(),
                            emuvdi_menu_height());

            aes_tree_out();
            aes_tree_done();
            break;

        case MENU_REMOVE:
            emuvdi_menu_bar(0, 0);

            bar_tree = 0;
            bar_shown = 0;

            gfx_window_close(BAR_WINDOW);
            break;

        default:
            /* Asking which application owns the bar, and the settings the
             * later versions of the AES added to it */
            halt_execution();
            printf("AES menu_bar was asked for %d, which is not implemented\n",
                   what);
            return AES_ERROR;
    }

    return AES_E_OK;
}

/* Where a menu has dropped down, which is the AES telling us through the one
 * place that knows: see emuvdi/gemmnlib.c */
void host_menu_begin(int16_t x, int16_t y, int16_t width, int16_t height)
{
    gfx_menu_open(x, y, width, height);
}

void host_menu_end(void)
{
    gfx_menu_close();
}

/* The three that change one entry ******************************************/

/*
 * Ticking an entry, greying one out, and putting a title back to normal after
 * something was chosen from it.
 *
 * All three are one bit of one object's state, so they are one function with
 * the differences written out. The tree goes across and comes back, because
 * what changed is a state and states are what get written back - the
 * application asked for this, and will draw the bar again expecting to see it.
 *
 * The last of them is not a nicety. The AES leaves a title held open after
 * something is chosen from it, deliberately, so that it still looks open while
 * the application does whatever was asked; putting it back is the
 * application's job, and until this call works there is no way to do it.
 */
static uint32_t menu_change(uint16_t bit, int16_t set, int16_t draw,
                            int16_t only_if_enabled, int16_t object)
{
    uint32_t address = aes_addrin(0);
    void *host;
    int16_t answer;

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    answer = emuvdi_menu_change(host, object, bit, set, draw, only_if_enabled);

    aes_tree_out();
    aes_tree_done();

    gem_present();

    return (uint32_t)(uint16_t)answer;
}

uint32_t AES_menu_icheck()
{
    int16_t item = aes_intin(0);
    int16_t ticked = aes_intin(1);

    FUNC_TRACE_ENTER_ARGS {
        printf("    item: %d, %s\n", item, ticked ? "ticked" : "not ticked");
    }

    return menu_change(CHECKED, ticked, 0, 0, item);
}

uint32_t AES_menu_ienable()
{
    int16_t item = aes_intin(0);
    int16_t enabled = aes_intin(1);

    FUNC_TRACE_ENTER_ARGS {
        printf("    item: %d, %s\n", item & 0x7fff,
               enabled ? "enabled" : "disabled");
    }

    /* The top bit of the item says to draw it now rather than leave it for
     * the next time the bar is drawn */
    return menu_change(DISABLED, !enabled, (item & 0x8000) != 0, 0,
                       item & 0x7fff);
}

uint32_t AES_menu_tnormal()
{
    int16_t title = aes_intin(0);
    int16_t normal = aes_intin(1);

    FUNC_TRACE_ENTER_ARGS {
        printf("    title: %d, %s\n", title, normal ? "normal" : "held open");
    }

    return menu_change(SELECTED, !normal, 1, 1, title);
}
