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
 * The system fonts.
 *
 * EmuTOS picks these in bios/font.c, which asks country.c which fonts a
 * machine sold in a given country should have. That is a question about a
 * machine rather than about drawing, and answering it would drag in the whole
 * localisation apparatus, so the three Atari ST fonts are taken directly
 * instead. They are the ones an application means when it asks for the system
 * font, and their metrics are what a GEM program lays itself out from.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"

/* The fonts themselves, compiled from the submodule */
extern const Fonthead fnt_st_6x6;
extern const Fonthead fnt_st_8x8;
extern const Fonthead fnt_st_8x16;

/*
 * The headers live in RAM because the VDI writes to them: scaling a font
 * builds a new header, and the chain below is edited rather than declared.
 */
Fonthead fon6x6;
Fonthead fon8x8;
Fonthead fon8x16;

/* The fonts the VDI knows about, which text_init fills in from the two it is
 * given here, and the one it settles on as the default */
const Fonthead *font_ring[4];
const Fonthead *def_font;
WORD font_count;

/* The array line-A hands out. Nothing reaches line-A yet, but the fonts are
 * the same three either way. */
const Fonthead *sysfonts[4];

/*
 * Only the 8x8 and 8x16 headers are chained, and 6x6 is left out of the chain
 * entirely. That is how TOS does it, and text_init depends on it: it walks the
 * chain to count how many different font ids there are, and a 6x6 in it would
 * make that count wrong.
 */
void host_font_init(void)
{
    fon6x6 = fnt_st_6x6;
    fon8x8 = fnt_st_8x8;
    fon8x16 = fnt_st_8x16;

    fon8x8.next_font = &fon8x16;
    fon8x16.next_font = NULL;
    fon6x6.next_font = NULL;

    font_count = 1;

    sysfonts[0] = &fnt_st_6x6;
    sysfonts[1] = &fnt_st_8x8;
    sysfonts[2] = &fnt_st_8x16;
    sysfonts[3] = NULL;

    /* Builds font_ring and def_font out of the above */
    text_init();
}
