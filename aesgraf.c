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
 * Graphics functions.
 *
 * graf_handle is how an application gets at the screen. It answers with the
 * workstation the AES already has open, and the application opens a virtual
 * one against it with v_opnvwk rather than opening a physical workstation of
 * its own. That is the whole shape of drawing under GEM: the AES says where,
 * the VDI does the drawing.
 *
 * graf_mouse is the other half of that bargain and is nearly nothing here. On
 * an ST the AES draws the pointer into the screen itself, so an application
 * has to ask for it to be taken away before drawing under it and put back
 * afterwards; here the desktop draws it, over a window rather than into one,
 * and there is nothing to take away.
 */

#include "aes_p.h"

#include "emuvdi/emuvdi.h"
#include "gem_p.h"
#include "gfx.h"

uint32_t AES_graf_handle()
{
    int16_t handle, wchar, hchar, wbox, hbox;

    FUNC_TRACE_ENTER

    emuvdi_graf_handle(&handle, &wchar, &hchar, &wbox, &hbox);

    /*
     * How wide and tall a character is, and how large a box has to be to hold
     * one. An application lays its dialogs out in these rather than in pixels,
     * which is what let the same resource file serve screens of different
     * resolutions.
     */
    aes_set_intout(1, wchar);
    aes_set_intout(2, hchar);
    aes_set_intout(3, wbox);
    aes_set_intout(4, hbox);

    FUNC_TRACE_ARGS {
        printf("    handle: %d, char: %dx%d, box: %dx%d\n",
               handle, wchar, hchar, wbox, hbox);
    }

    return handle;
}

/* The shapes graf_mouse knows about, http://toshyp.atari.org/en/008003.html */
#define ARROW         (0)
#define TEXT_CRSR     (1)
#define BUSY_BEE      (2)
#define POINT_HAND    (3)
#define FLAT_HAND     (4)
#define THIN_CROSS    (5)
#define THICK_CROSS   (6)
#define OUTLN_CROSS   (7)
#define USER_DEF    (255)
#define M_OFF       (256)
#define M_ON        (257)

/* Which one was last asked for, so that hiding and showing put back what was
 * there rather than an arrow */
static int16_t shape = ARROW;

uint32_t AES_graf_mouse()
{
    int16_t wanted = aes_intin(0);

    FUNC_TRACE_ENTER_ARGS {
        printf("    shape: %d\n", wanted);
    }

    switch (wanted)
    {
        case M_OFF:
        case M_ON:
            /*
             * Taking the pointer away and putting it back, which an
             * application does around its own drawing so as not to draw over
             * the arrow or leave a hole where it was.
             *
             * There is nothing to do. The pointer is the desktop's and is
             * drawn over the window rather than into it, so drawing underneath
             * it cannot disturb it - which is also why an application that
             * forgets to put it back does not leave the screen without one.
             */
            break;

        case USER_DEF:
            /*
             * A shape of the application's own, as a MFORM: two sixteen by
             * sixteen bitmaps and the point in them that counts as where the
             * pointer is. Nothing is done with it yet - saying so would mean
             * turning it into a buffer the compositor understands and handing
             * it over on the seat - so the pointer keeps whatever it had.
             */
            shape = USER_DEF;
            break;

        default:
            if (wanted < ARROW || wanted > OUTLN_CROSS)
                return AES_ERROR;

            /*
             * One of the eight the AES has always had. Each has a plain
             * equivalent in the set every desktop offers, so this will one day
             * be a name said to the compositor rather than a bitmap drawn.
             */
            shape = wanted;
            break;
    }

    return AES_E_OK;
}


/* The boxes an application drags about *************************************/

/*
 * All of these follow the mouse and draw an outline exclusive-ored on and off
 * again, which is how a drag was shown on a machine that could not afford to
 * redraw anything behind it. They are EmuTOS's, unchanged, because what they
 * do is arithmetic and drawing and neither has anything to do with there being
 * a compositor.
 *
 * Where the outline can be seen is another matter. It is drawn into the screen
 * the AES keeps, and that screen is only shown where a window is showing it -
 * so a rubber band pulled out inside a window works, and one pulled across the
 * desktop is followed correctly and seen by nobody. That is the same gap the
 * dialogs had before form_dial gave them windows, and it closes the same way.
 */

uint32_t AES_graf_rubberbox()
{
    int16_t x = aes_intin(0);
    int16_t y = aes_intin(1);
    int16_t wmin = aes_intin(2);
    int16_t hmin = aes_intin(3);
    int16_t w = 0, h = 0;

    FUNC_TRACE_ENTER_ARGS {
        printf("    from %d,%d, at least %dx%d\n", x, y, wmin, hmin);
    }

    if (!gem_start())
        return AES_ERROR;

    emuvdi_graf_rubberbox(x, y, wmin, hmin, &w, &h);

    aes_set_intout(1, w);
    aes_set_intout(2, h);

    return AES_E_OK;
}

uint32_t AES_graf_dragbox()
{
    int16_t w = aes_intin(0);
    int16_t h = aes_intin(1);
    int16_t x = aes_intin(2);
    int16_t y = aes_intin(3);
    int16_t bx = aes_intin(4);
    int16_t by = aes_intin(5);
    int16_t bw = aes_intin(6);
    int16_t bh = aes_intin(7);
    int16_t ex = 0, ey = 0;

    FUNC_TRACE_ENTER_ARGS {
        printf("    %dx%d from %d,%d inside %d,%d %dx%d\n",
               w, h, x, y, bx, by, bw, bh);
    }

    if (!gem_start())
        return AES_ERROR;

    emuvdi_graf_dragbox(w, h, x, y, bx, by, bw, bh, &ex, &ey);

    aes_set_intout(1, ex);
    aes_set_intout(2, ey);

    return AES_E_OK;
}

uint32_t AES_graf_movebox()
{
    FUNC_TRACE_ENTER

    if (!gem_start())
        return AES_ERROR;

    emuvdi_graf_movebox(aes_intin(0), aes_intin(1), aes_intin(2),
                        aes_intin(3), aes_intin(4), aes_intin(5));

    return AES_E_OK;
}

uint32_t AES_graf_growbox()
{
    FUNC_TRACE_ENTER

    if (!gem_start())
        return AES_ERROR;

    emuvdi_graf_growbox(aes_intin(0), aes_intin(1), aes_intin(2), aes_intin(3),
                        aes_intin(4), aes_intin(5), aes_intin(6), aes_intin(7));

    return AES_E_OK;
}

uint32_t AES_graf_shrinkbox()
{
    FUNC_TRACE_ENTER

    if (!gem_start())
        return AES_ERROR;

    emuvdi_graf_shrinkbox(aes_intin(0), aes_intin(1), aes_intin(2),
                          aes_intin(3), aes_intin(4), aes_intin(5),
                          aes_intin(6), aes_intin(7));

    return AES_E_OK;
}

/*
 * Watching a box until the mouse leaves it or the button comes up, which is
 * how an application finds out that one of its own buttons was pressed rather
 * than merely pointed at. form_do is a loop over this one.
 */
uint32_t AES_graf_watchbox()
{
    uint32_t address = aes_addrin(0);

    /* From one rather than from nought. The first word is the one graf_slidebox
     * puts the parent object in, and watchbox has no parent to put there, but
     * it is still the word the arguments start after: a binding leaves it
     * alone rather than closing the gap. */
    int16_t obj = aes_intin(1);
    int16_t instate = aes_intin(2);
    int16_t outstate = aes_intin(3);
    void *host;
    int16_t inside;

    FUNC_TRACE_ENTER_ARGS {
        printf("    tree 0x%x, object %d\n", address, obj);
    }

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    inside = emuvdi_graf_watchbox(host, obj, instate, outstate);

    aes_tree_out();
    aes_tree_done();

    gem_present();

    return (uint32_t)(uint16_t)inside;
}

/* Dragging a slider inside its bar, and answering where it ended up in
 * thousandths of the way along */
uint32_t AES_graf_slidebox()
{
    uint32_t address = aes_addrin(0);
    int16_t parent = aes_intin(0);
    int16_t obj = aes_intin(1);
    int16_t vertical = aes_intin(2);
    void *host;
    int16_t where;

    FUNC_TRACE_ENTER_ARGS {
        printf("    tree 0x%x, %d inside %d, %s\n", address, obj, parent,
               vertical ? "up and down" : "across");
    }

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    where = emuvdi_graf_slidebox(host, parent, obj, vertical);

    aes_tree_out();
    aes_tree_done();

    gem_present();

    return (uint32_t)(uint16_t)where;
}

/*
 * graf_mkstate - where the mouse is and what is held down, right now
 *
 * Asked rather than waited for, which is the whole point of it: an application
 * that is drawing wants to know where the pointer got to without stopping to
 * wait for it to go somewhere.
 */
uint32_t AES_graf_mkstate()
{
    int16_t x = 0, y = 0, buttons = 0;

    FUNC_TRACE_ENTER

    if (!gem_start())
        return AES_ERROR;

    gfx_mouse(&x, &y, &buttons);

    aes_set_intout(1, x);
    aes_set_intout(2, y);
    aes_set_intout(3, buttons);
    aes_set_intout(4, (int16_t)gfx_kstate());

    FUNC_TRACE_ARGS {
        printf("    %d,%d buttons %d\n", x, y, buttons);
    }

    return AES_E_OK;
}
