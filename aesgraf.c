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
