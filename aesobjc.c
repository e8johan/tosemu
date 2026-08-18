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
 * Objects.
 *
 * Every dialog, every menu and every button in GEM is an object tree, and
 * drawing one is what the AES is for as much as windows are. The drawing
 * itself is EmuTOS's, which is the reason its object library is carried at
 * all; what is here is getting the tree from the machine to where that library
 * can read it, and the answers back again.
 */

#include "aes_p.h"

#include "gem_p.h"
#include "emuvdi/emuvdi.h"
#include "tossystem.h"
#include "m68k.h"

/*
 * form_do - run a dialog until something ends it
 *
 * The tree goes across, the AES loops over the waiting an application would do
 * for itself, and what comes back is which object ended it. The tree comes
 * back too: the button that was pressed is SELECTED in it, and an edited field
 * holds what was typed.
 */
uint32_t AES_form_do()
{
    uint32_t address = aes_addrin(0);
    int16_t start = aes_intin(0);
    void *host;
    int16_t ended;

    FUNC_TRACE_ENTER_ARGS {
        printf("    tree: 0x%x, editing from %d\n", address, start);
    }

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    ended = emuvdi_form_do(host, start);

    aes_tree_out();
    aes_tree_done();

    return (uint32_t)ended;
}

uint32_t AES_objc_draw()
{
    uint32_t address = aes_addrin(0);
    int16_t start = aes_intin(0);
    int16_t depth = aes_intin(1);
    int16_t x = aes_intin(2);
    int16_t y = aes_intin(3);
    int16_t w = aes_intin(4);
    int16_t h = aes_intin(5);
    void *host;

    FUNC_TRACE_ENTER_ARGS {
        printf("    tree: 0x%x, from %d, %d deep, clip %d,%d %dx%d\n",
               address, start, depth, x, y, w, h);
    }

    if (!gem_start())
        return AES_ERROR;

    host = aes_tree_in(address);
    if (!host)
        return AES_ERROR;

    emuvdi_objc_draw(host, start, depth, x, y, w, h);

    /* Drawing does not change a tree, but it costs nothing to put back what
     * did not move, and it keeps every one of these the same shape */
    aes_tree_out();
    aes_tree_done();

    return AES_E_OK;
}

/*
 * What the alert in emuvdi/gemfmalt.c reaches when it puts its window up and
 * takes it away. The same two things form_dial does, so that an alert and a
 * dialog behave alike.
 */
void host_dialog_begin(int16_t x, int16_t y, int16_t width, int16_t height)
{
    gem_dialog_begin(x, y, width, height);
}

void host_dialog_end(void)
{
    gem_dialog_end();
}

/* form_alert **************************************************************/

/*
 * form_alert - the box with an icon, some text and up to three buttons
 *
 * The whole alert is one string, packed the way an application writes it:
 * "[1][Something went wrong][OK|Cancel]". The AES takes it apart, builds a
 * tree out of it, centres it and runs it, and answers with which button was
 * pressed counting from one.
 *
 * The string is in the machine, so it comes across first. Unlike form_dial
 * this does not reserve the screen: an alert saves what is under it and puts
 * it back afterwards, which is a raster copy in each direction.
 */
uint32_t AES_form_alert()
{
    int16_t defbut = aes_intin(0);
    uint32_t address = aes_addrin(0);
    char text[512];
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    default button %d, text at 0x%x\n", defbut, address);
    }

    if (!gem_start())
        return AES_ERROR;

    for (i = 0; i < (int)sizeof text - 1; i++)
    {
        text[i] = (char)m68k_read_memory_8(address + i);
        if (text[i] == 0)
            break;
    }
    text[i] = 0;

    return (uint32_t)emuvdi_form_alert(defbut, text);
}

/* form_dial ***************************************************************/

/*
 * form_dial - reserve or release the screen a dialog sits on
 *
 * On a real machine this is how an application asks the AES to remember what
 * is under a dialog so that it can be put back afterwards. FMD_START says a
 * rectangle is about to be covered, FMD_FINISH says it is free again, and the
 * two in between draw the box that grows and shrinks as a dialog appears.
 *
 * Here the rectangle becomes a window of the compositor's, which is what makes
 * a GEM dialog behave like a dialog: kept above the window it belongs to, the
 * parent out of reach while it is up, and movable with whatever the desktop
 * uses for moving windows even though the application inside it is blocked.
 *
 * Nothing needs remembering, because nothing is covered: the screen behind is
 * still there, in the other window, exactly as it was.
 *
 * http://toshyp.atari.org/en/007005.html
 */
#define FMD_START  (0)
#define FMD_GROW   (1)
#define FMD_SHRINK (2)
#define FMD_FINISH (3)

uint32_t AES_form_dial()
{
    int16_t what = aes_intin(0);
    int16_t x = aes_intin(5);
    int16_t y = aes_intin(6);
    int16_t width = aes_intin(7);
    int16_t height = aes_intin(8);

    FUNC_TRACE_ENTER_ARGS {
        printf("    %d, %d,%d %dx%d\n", what, x, y, width, height);
    }

    if (!gem_start())
        return AES_ERROR;

    switch (what)
    {
        case FMD_START:
            gem_dialog_begin(x, y, width, height);
            break;

        case FMD_FINISH:
            gem_dialog_end();
            break;

        case FMD_GROW:
        case FMD_SHRINK:
            /*
             * The box that grows out of nothing and shrinks back into it. It
             * was there to show where a dialog came from on a screen that
             * could not move windows; a compositor has its own way of showing
             * that, and drawing this one over the top would fight it.
             */
            break;

        default:
            halt_execution();
            printf("AES form_dial was asked for %d, which is not one of the "
                   "four it has\n", what);
            return AES_ERROR;
    }

    return AES_E_OK;
}
