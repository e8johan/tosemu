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
