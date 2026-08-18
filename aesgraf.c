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
