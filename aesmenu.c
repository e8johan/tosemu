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
 * The dropped-down menu is drawn below the bar, past the bottom of that strip.
 * While one is down the window is made tall enough to show it, and shrinks
 * again afterwards - which is why the menu is run inside a window of its own
 * size rather than the bar's.
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

/* The window handle the bar is shown in. The AES gives handles to windows an
 * application asked for, and it did not ask for this one. */
#define BAR_WINDOW (8)

/* Where the tree came from, so that what the AES changed in it - a ticked
 * entry, a greyed out one - goes back to the application */
static uint32_t bar_tree;
static int bar_shown;

void aes_menu_reset()
{
    bar_tree = 0;
    bar_shown = 0;
}

/*
 * Whether a point is in the menu bar, which is what decides between running
 * the menu and handing the click to the application.
 */
int aes_menu_hit(int16_t x, int16_t y)
{
    (void)x;

    if (!bar_shown)
        return 0;

    return y < emuvdi_menu_height();
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

    emuvdi_menu_bar(host, 1);

    /* The menu drops below the bar, so the window has to be tall enough to
     * show it while it is down */
    gfx_window_move(BAR_WINDOW, 0, 0, emuvdi_screen_width(),
                    emuvdi_screen_height());

    if (emuvdi_menu_do(&title, &item))
    {
        for (i = 0; i < 8; i++)
            message[i] = 0;

        message[0] = MN_SELECTED;
        message[3] = title;
        message[4] = item;

        aes_message_post(message);
    }

    /* And back to being a strip */
    gfx_window_move(BAR_WINDOW, 0, 0, emuvdi_screen_width(),
                    emuvdi_menu_height());

    aes_tree_out();
    aes_tree_done();
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

    /*
     * Not yet. Everything below this works as far as handing the tree to
     * EmuTOS's menu library, and that is where it stops: mn_bar walks a menu
     * tree by fixed indices - the screen, the bar, the active area, then the
     * box holding the menus and the Desk menu inside it - and splices entries
     * into the Desk menu for whichever accessories have registered.
     *
     * A tree from a resource file has that shape and blank entries waiting to
     * be filled in. One built by hand does not, and mn_bar walks off it. The
     * copy has room after the end of it now, so nothing is corrupted, but the
     * shape is what has to be got right, and checking that is the next piece
     * of work rather than this one.
     *
     * Stopping here says so, which is better than drawing something wrong or
     * falling over.
     */
    halt_execution();
    printf("AES menu_bar is not implemented yet: the menu library is built "
           "and wired, but a menu tree has to have the shape mn_bar walks "
           "and that is not checked yet\n");
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
