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
 * The GEM trap.
 *
 * GEMDOS, BIOS and XBIOS take their arguments on the stack, so their handlers
 * read them from where a7 points. GEM does not. Both halves take a single
 * pointer in d1, to a block of pointers to the arrays that carry the arguments
 * and the answers, and put the function number inside one of those arrays
 * rather than in a register. d0 holds only which half of GEM is being asked.
 *
 * The arrays are the reason the two halves cannot share a dispatcher beyond
 * this file: an AES parameter block is six pointers and a VDI one is five, and
 * they do not describe the same things.
 */

#include "gem.h"

#include <stdio.h>
#include <stdlib.h>

#include "gem_p.h"
#include "tossystem.h"
#include "surface.h"
#include "aes_p.h"
#include "aesclient.h"
#include "screen.h"
#include "settings.h"
#include "gfx.h"
#include "emuvdi/emuvdi.h"
#include "m68k.h"

/*
 * The screen, which both halves of GEM draw on and which neither owns.
 *
 * Which screen it is comes from screen_mode - see the note there - and the VDI
 * works out which resolution to report from how many planes this has, so the
 * two cannot disagree.
 */
static struct surface *screen;

/*
 * Where a dialog draws.
 *
 * A dialog gets a surface of its own rather than a rectangle of the screen's,
 * so that what it draws does not also appear in the window behind it. It is
 * the same size as the screen, which means an application goes on drawing at
 * the coordinates it chose and the drawing lands where it expects; only which
 * memory it lands in has changed.
 *
 * It starts as a copy of the screen, so that any part of the dialog's window
 * the dialog itself does not cover shows what was behind it rather than
 * nothing.
 */
static struct surface *dialog;

/*
 * And where a menu that has dropped down draws, which is the same idea again.
 *
 * A menu covers whatever is behind it, and what is behind it here is somebody's
 * window rather than a picture of a screen: a menu drawn into the screen
 * appears inside every window showing that part of it, which is exactly what
 * having real windows was meant to avoid. So it gets a surface, the size of the
 * screen and starting as a copy of it, and the window it is shown in is the
 * only place its pixels are ever seen.
 *
 * It is not on the dialog stack because it is not one of those. A menu is up
 * while an application is inside a wait rather than inside a dialog, and the
 * two do not nest: nothing puts a dialog up from a menu that is still down.
 */
static struct surface *menu;

static int started;

/* The screen this session was asked for, for when there is no daemon to say */
void gem_default_screen(int16_t *width, int16_t *height, int16_t *planes)
{
    screen_mode(width, height, planes);
}

/*
 * Readies GEM, the first time anything asks for it.
 *
 * Either half can be the first to be called: an application that draws without
 * a window reaches the VDI first, and one that opens a window reaches the AES
 * first, so neither can be the one to set the other up.
 */
int gem_start()
{
    int16_t width, height, planes;

    if (started)
        return 1;

    /*
     * How large the screen is, which the daemon says when there is one.
     *
     * It has to be one screen for everything running: applications lay their
     * windows out in it and are told where the others put theirs, so two that
     * disagree about its size disagree about everything. With nobody to ask,
     * it is what this build was made with.
     */
    aes_client_screen(&width, &height, &planes);

    screen = surface_create((uint16_t)width, (uint16_t)height,
                            (uint16_t)planes);
    if (!screen)
    {
        halt_execution();
        printf("GEM: no room for a %dx%d screen of %d planes\n",
               width, height, planes);
        return 0;
    }

    surface_select(screen);
    emuvdi_init();

    /*
     * A window to show it in, if there is a compositor to open one on. There
     * usually is not while a test is running, and the emulator is no different
     * for it: the screen is in memory either way, and this only decides
     * whether anyone can see it.
     */
    gfx_open(screen);

    started = 1;

    return 1;
}

/*
 * Puts what has been drawn where it can be seen.
 *
 * Two ways of seeing it, and they are independent: a window, when there is a
 * compositor, and a file, when somebody asked for one. The file is not a
 * lesser version of the window - it is how the screen is looked at from a
 * terminal, or from a test, or by whoever is reading this without a desktop
 * in front of them.
 */
/*
 * Reserves the screen for a dialog, and gives it back.
 *
 * This is what form_dial does on an ST, where the AES remembers what is under
 * the dialog and puts it back afterwards. Here nothing is covered - the screen
 * is still there, in its own window and its own memory - so what the two do
 * instead is put the dialog in a window of its own and take it away again.
 *
 * They nest, because dialogs do. The file selector is a dialog and it puts an
 * alert up when it cannot read a directory; an application does the same when
 * something goes wrong in the middle of one of its own. With one dialog at a
 * time the alert takes the selector's window away and the selector has nothing
 * to draw into when the alert has gone - which looks exactly like everything
 * stopping, because nothing it does afterwards can be seen.
 *
 * A new one starts as a copy of whatever is being drawn into rather than of
 * the screen, so an alert over the selector has the selector behind it.
 */
#define DIALOGS (4)

static struct {
    struct surface *shows;
    int16_t x, y, w, h;
} stack[DIALOGS];

static int depth;

void gem_dialog_begin(int16_t x, int16_t y, int16_t width, int16_t height)
{
    struct surface *below = surface_selected();
    struct surface *shows;

    if (!started || width <= 0 || height <= 0)
        return;

    if (depth >= DIALOGS)
    {
        /* Deeper than anything reasonable does. Answering by drawing this one
         * where the last one was is better than not drawing it at all. */
        gem_dialog_end();
    }

    shows = surface_create(surface_width(screen), surface_height(screen),
                           surface_planes(screen));
    if (!shows)
        return;

    surface_copy(shows, below ? below : screen);

    /* Only one of them is shown at a time, so the one underneath goes away
     * while this one is up and comes back when it is done with */
    if (depth > 0)
        gfx_dialog_close();

    stack[depth].shows = shows;
    stack[depth].x = x;
    stack[depth].y = y;
    stack[depth].w = width;
    stack[depth].h = height;
    depth++;

    dialog = shows;
    surface_select(dialog);

    gfx_dialog_open(dialog, x, y, width, height);
}

void gem_dialog_end()
{
    if (depth <= 0)
        return;

    gfx_dialog_close();

    depth--;
    surface_free(stack[depth].shows);
    stack[depth].shows = 0;

    if (depth > 0)
    {
        /* The one underneath, which was there before this one covered it */
        dialog = stack[depth-1].shows;
        surface_select(dialog);

        gfx_dialog_open(dialog, stack[depth-1].x, stack[depth-1].y,
                        stack[depth-1].w, stack[depth-1].h);
    }
    else
    {
        dialog = 0;
        surface_select(screen);
    }
}

/*
 * Somewhere for a menu to be drawn, and letting go of it again.
 *
 * The AES asks for this where it used to save what was under the menu, which
 * is the one moment that is both after the bar has been drawn and before the
 * menu has - see the note in emuvdi/gemmnlib.c. From here until the menu goes
 * away everything the AES draws lands on the menu's surface, which is what
 * keeps it out of the screen and so out of every window showing the screen.
 *
 * A copy of whatever is being drawn into rather than of the screen, for the
 * same reason a dialog is: what is behind the menu shows through the parts of
 * its window the menu itself does not cover.
 */
void gem_menu_begin(void)
{
    struct surface *below = surface_selected();

    if (!started || menu)
        return;

    menu = surface_create(surface_width(screen), surface_height(screen),
                          surface_planes(screen));
    if (!menu)
        return;

    surface_copy(menu, below ? below : screen);

    surface_select(menu);
}

void gem_menu_end(void)
{
    if (!menu)
        return;

    surface_free(menu);
    menu = 0;

    /* Back to whatever was being drawn into before, which is a dialog if one
     * is up and the screen if none is */
    surface_select(dialog ? dialog : screen);
}

struct surface *gem_menu_surface(void)
{
    return menu;
}

/*
 * The screen, for the things that belong to it whatever else is being drawn
 * into.
 *
 * Most of what the AES draws belongs wherever the drawing is going: a dialog
 * reserves the screen, everything after that lands on the dialog's surface,
 * and that is what keeps a dialog out of the window behind it. A window's
 * frame is the exception. It is drawn where the window is and the window shows
 * the screen, so a frame drawn while a dialog is up has to go past the dialog
 * and onto the screen - see draw_frame in aeswind.c, which is what asks.
 */
struct surface *gem_screen_surface(void)
{
    return screen;
}

/*
 * What a child of fork has to do before it is a program of its own.
 *
 * It has a copy of everything this one had: a connection to the compositor
 * showing the parent's windows, a socket the daemon knows the parent by, and a
 * screen with the parent's drawing in it. None of it is the child's and none of
 * it may be used, so it is all let go of. The child builds its own when it
 * calls appl_init, exactly as it would have if it had been started on its own.
 */
void gem_forget(void)
{
    gfx_forget();
    aes_client_forget();

    if (screen)
        surface_free(screen);
    screen = 0;

    if (menu)
        surface_free(menu);
    menu = 0;

    while (depth > 0)
    {
        depth--;
        surface_free(stack[depth].shows);
        stack[depth].shows = 0;
    }
    dialog = 0;

    started = 0;

    aes_appl_reset();
}

void gem_present()
{
    const char *shot;

    if (!started)
        return;

    shot = setting("TOSEMU_SCREENSHOT");
    if (shot)
    {
        /* Whichever is being drawn into. A dialog's surface starts as a copy
         * of the screen and a menu's as a copy of that, so whichever of them
         * is on top is the whole picture rather than half of it. */
        surface_write_ppm(menu ? menu : dialog ? dialog : screen, shot);
    }

    gfx_present();
}

void gem_trap()
{
    int16_t which = (int16_t)(m68k_get_reg(0, M68K_REG_D0) & 0xffff);

    switch (which)
    {
        case GEM_AES:
            aes_trap();
            break;
        case GEM_VDI:
            vdi_trap();
            break;
        case GEM_GDOS:
            /* Answered by saying nothing, which is the answer: d0 is left as
             * the caller set it and that is what tells it there is no GDOS.
             * There is none - see the TODO - and a program that asks is one
             * that is prepared to be told so. */
            break;
        default:
            halt_execution();
            printf("GEM called with 0x%x in d0, which is neither the AES (0x%x) "
                   "nor the VDI (0x%x)\n", which, GEM_AES, GEM_VDI);
            break;
    }
}

void gem_reset()
{
    aes_reset();
    vdi_reset();

    gem_menu_end();
    gem_dialog_end();
    gfx_close();

    surface_free(screen);
    screen = 0;
    started = 0;
}

/* Parameter block arrays ***************************************************/

/*
 * An index outside the array the caller declared is a bug in this emulator
 * rather than in the application: the count comes from the control array the
 * application filled in, and a handler reading past it has misunderstood its
 * own function. Say so and read zero, which is a great deal easier to follow
 * than the arbitrary word that happened to be there.
 */
static int gem_in_range(uint32_t array, int count, int index)
{
    if (array == 0)
    {
        printf("GEM: parameter block array is a null pointer\n");
        return 0;
    }

    if (index < 0 || index >= count)
    {
        printf("GEM: index %d is outside the %d entry array at 0x%x\n",
               index, count, array);
        return 0;
    }

    return 1;
}

int16_t gem_word(uint32_t array, int count, int index)
{
    if (!gem_in_range(array, count, index))
        return 0;

    return (int16_t)m68k_read_memory_16(array + 2*index);
}

void gem_set_word(uint32_t array, int count, int index, int16_t value)
{
    if (!gem_in_range(array, count, index))
        return;

    m68k_write_memory_16(array + 2*index, (uint16_t)value);
}

uint32_t gem_long(uint32_t array, int count, int index)
{
    if (!gem_in_range(array, count, index))
        return 0;

    return m68k_read_memory_32(array + 4*index);
}

void gem_set_long(uint32_t array, int count, int index, uint32_t value)
{
    if (!gem_in_range(array, count, index))
        return;

    m68k_write_memory_32(array + 4*index, value);
}
