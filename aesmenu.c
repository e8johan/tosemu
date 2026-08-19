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

#include <stdio.h>
#include <string.h>

#include "aesclient.h"
#include "aesproto.h"
#include "gem_p.h"
#include "gfx.h"
#include "emuvdi/emuvdi.h"
#include "tossystem.h"
#include "m68k.h"

/* What menu_bar is asked to do, http://toshyp.atari.org/en/005005.html */
#define MENU_REMOVE  (0)
#define MENU_INSTALL (1)

/* The message an application is sent when something is chosen from a menu, and
 * the two an accessory is sent when it is asked for and told to go away */
#define MN_SELECTED (10)
#define AC_OPEN     (40)
#define AC_CLOSE    (41)

/* Which title the Desk menu is. It is the first, always, because the AES puts
 * its own entries in it and has to know where they went. */
#define DESK_TITLE  (3)

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

/*
 * The accessories, as this application sees them.
 *
 * The names are kept here because the AES keeps the pointer rather than the
 * words, so they have to stay put for as long as a bar is up. Which
 * application each belongs to is kept beside them, because that is who a
 * message goes to when somebody picks one, and the AES has no idea - on a
 * machine with one address space it was a process descriptor, and here it is
 * an application on the other end of a socket.
 */
#define MAX_ACCS (6)

static struct {
    char name[16];
    int16_t owner;
} accessory[MAX_ACCS];

static int16_t accessories;

/*
 * Tells the AES who they are, just before a bar goes up.
 *
 * Done then rather than when the list arrives, because the AES splices them
 * into whatever tree is being put up and there is no tree at any other time.
 * An application that has had its bar up since before an accessory started
 * gets it the next time it puts one up, which is what happens on an ST too.
 */
static void accessories_into_the_menu(void)
{
    int16_t i;

    emuvdi_menu_forget_accessories();

    accessories = aes_client_accessories();
    if (accessories > MAX_ACCS)
        accessories = MAX_ACCS;

    for (i = 0; i < accessories; i++)
    {
        const char *name = aes_client_accessory_name(i);

        snprintf(accessory[i].name, sizeof accessory[i].name, "  %s",
                 name ? name : "");
        accessory[i].owner = aes_client_accessory_owner(i);

        emuvdi_menu_register(accessory[i].name);
    }
}

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

    /* The accessories again, because putting the bar up is what splices them
     * in and this is putting it up: the list may also have changed since the
     * application last did */
    accessories_into_the_menu();

    emuvdi_menu_bar(host, 1);

    if (emuvdi_menu_do(&title, &item))
    {
        int16_t first = emuvdi_menu_first_accessory();

        for (i = 0; i < 8; i++)
            message[i] = 0;

        /*
         * An accessory, or something of the application's own.
         *
         * The entries the AES spliced into the Desk menu are not the
         * application's and it has never heard of them, so picking one is not
         * MN_SELECTED - it is AC_OPEN, and it goes to the accessory rather
         * than to the application whose menu bar it was in.
         */
        if (title == DESK_TITLE && accessories > 0
            && item >= first && item < first + accessories)
        {
            int16_t which = item - first;

            message[0] = AC_OPEN;
            message[3] = which;         /* which of its own menu entries */

            aes_client_send(accessory[which].owner, message);
        }
        else
        {
            message[0] = MN_SELECTED;
            message[3] = title;
            message[4] = item;

            aes_message_post(message);
        }
    }

    bar_running = 0;
    was_among_titles = 1;      /* still there; do not start again on the spot */

    aes_tree_out();
    aes_tree_done();
    emuvdi_menu_forget_tree();

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

            accessories_into_the_menu();

            emuvdi_menu_bar(host, 1);

            bar_tree = address;
            bar_shown = 1;

            gfx_window_open(BAR_WINDOW, "Menu", 0, 0, emuvdi_screen_width(),
                            emuvdi_menu_height());

            aes_tree_out();
            aes_tree_done();
            emuvdi_menu_forget_tree();
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


/* menu_text ***************************************************************/

/*
 * Changing what a menu entry says.
 *
 * The new words are copied into the string the entry already points at rather
 * than the entry being pointed somewhere else, which is what GEM does and what
 * an application expects: the string belongs to the application, usually
 * inside its resource, and it is entitled to have kept a pointer to it.
 *
 * Done in the machine's own memory, because both the tree and the string are
 * there and nothing has to come across to copy one into the other.
 */
uint32_t AES_menu_text()
{
    uint32_t tree = aes_addrin(0);
    uint32_t from = aes_addrin(1);
    int16_t item = aes_intin(0);
    uint32_t to;
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    item %d\n", item);
    }

    if (!tree || !from || item < 0)
        return AES_ERROR;

    /* Where the entry's words are now. An entry is a string object, so its
     * spec is the address of the words rather than anything about them. */
    to = m68k_read_memory_32(tree + item*24 + 12);
    if (!to)
        return AES_ERROR;

    /*
     * As many characters as were there, and no more. The string was made the
     * size of the longest thing the application meant to put in it, and there
     * is nothing here that knows how large that was - so a longer one is cut
     * rather than written past the end of somebody's resource.
     */
    for (i = 0; i < 256; i++)
    {
        uint8_t c = (uint8_t)m68k_read_memory_8(from + i);
        uint8_t was = (uint8_t)m68k_read_memory_8(to + i);

        if (was == 0)
            break;

        m68k_write_memory_8(to + i, c);

        if (c == 0)
            break;
    }

    return AES_E_OK;
}


/* menu_register ***********************************************************/

/*
 * An accessory saying what it is called.
 *
 * It is the first thing one does: an accessory has no window and no menu bar
 * of its own, so until it has said this there is no way to reach it and
 * nothing will ever happen to it. What it gets back is the number of its own
 * entry, which is how it tells its entries apart when there is more than one -
 * an accessory is allowed up to six.
 *
 * The name goes to the daemon rather than into a menu here, because the menu
 * it appears in belongs to whichever applications are running, not to this
 * one. They are told, and they splice it in the next time they put a bar up.
 */
uint32_t AES_menu_register()
{
    int16_t who = aes_intin(0);
    uint32_t address = aes_addrin(0);
    char name[AESD_NAME_LEN + 1];
    int i;

    for (i = 0; i < AESD_NAME_LEN; i++)
    {
        name[i] = (char)m68k_read_memory_8(address + i);

        if (name[i] == 0)
            break;
    }
    for (; i < AESD_NAME_LEN; i++)
        name[i] = ' ';
    name[AESD_NAME_LEN] = 0;

    FUNC_TRACE_ENTER_ARGS {
        printf("    application %d is [%s]\n", who, name);
    }

    /*
     * Negative is an accessory taking its entry away again, which one does
     * when it is asked to stop. There is nothing to take away yet, because
     * nothing here starts accessories - see the TODO.
     */
    if (who < 0)
        return AES_ERROR;

    aes_client_accessory(name);

    /* The first entry, this being the only one an accessory gets here. GEM
     * allowed several and answered with which; when that matters, the daemon
     * is what has to remember it. */
    return 0;
}
