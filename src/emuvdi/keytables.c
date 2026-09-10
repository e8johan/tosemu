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
 * The keyboard table an ST keyboard had.
 *
 * Three arrays of a hundred and twenty eight bytes, one for each key of the
 * machine, saying what that key types: unshifted, shifted, and with caps lock
 * down. An application reads them through XBIOS Keytbl, and what it usually
 * wants is the question the other way round - which letter is on the key that
 * arrived - because a menu shortcut is a key rather than a character.
 *
 * The tables are EmuTOS's, taken directly rather than through bios/font.c's
 * neighbour country.c, which picks a set for the country a machine was sold in.
 * That is the same trade fonts.c makes and for the same reason: the choice is a
 * question about a machine and answering it would drag in the whole
 * localisation apparatus. The American set is the one taken because it is the
 * one the keys of a modern keyboard are arranged in, near enough, on every
 * layout that has letters where QWERTY has them.
 *
 * It is a fallback rather than the answer. What tosemu hands an application is
 * built from the layout the person is actually typing on when there is a
 * desktop to ask - see gfx_keyboard_table - so that the letter printed on the
 * key is the letter the application is told about. This is what there is to
 * hand over when there is nobody to ask, which is a test, a terminal, or a
 * machine with nobody logged in.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"
#include "ikbd.h"

#include <stdint.h>

#include "keyb_us.h"

void emuvdi_keyboard_tables(const uint8_t **norm, const uint8_t **shift,
                            const uint8_t **caps)
{
    *norm = keytbl_us.norm;
    *shift = keytbl_us.shft;
    *caps = keytbl_us.caps;
}
