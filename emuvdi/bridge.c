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
 * Between tosemu and the ported VDI.
 *
 * This is the only file that is on both sides. It is built with the EmuTOS
 * flags and headers like the rest of emuvdi, and says nothing about them in
 * emuvdi.h, so tosemu can call the VDI without a WORD or a Vwk ever reaching
 * it.
 *
 * There is very little to do, because EmuTOS's own dispatcher does it. screen()
 * in vdi_main.c reads the control array, finds the workstation the handle
 * names, fills in how many answers there are and calls the function. All that
 * is left is to say where the arrays are.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"

#include "emuvdi.h"

/* EmuTOS's dispatcher, vdi_main.c */
void screen(void);

/* Ours, in fonts.c and hostvars.c */
void host_font_init(void);
void host_surface_select(void *base, UWORD width, UWORD height, UWORD planes);

void emuvdi_init()
{
    host_font_init();
}

void emuvdi_surface_select(void *base, uint16_t width, uint16_t height,
                           uint16_t planes)
{
    host_surface_select(base, width, height, planes);
}

void emuvdi_call(int16_t *control, int16_t *intin, int16_t *ptsin,
                 int16_t *intout, int16_t *ptsout)
{
    CONTRL = control;
    INTIN = intin;
    PTSIN = ptsin;
    INTOUT = intout;
    PTSOUT = ptsout;

    screen();
}

/*
 * The two ranges EmuTOS's jump tables cover, 1 to 39 and 100 to 134. screen()
 * simply returns for anything else, which would leave a caller waiting for an
 * answer that never comes and no sign of why, so tosemu asks first.
 */
int emuvdi_implements(int16_t opcode)
{
    if (opcode >= 1 && opcode <= 39)
        return 1;

    if (opcode >= 100 && opcode <= 134)
        return 1;

    return 0;
}
