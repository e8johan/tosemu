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
 * How the AES calls the VDI. Replaces EmuTOS's aes/gsx2.c.
 *
 * The original is thirty three lines: it points the parameter block at the
 * control array and executes a trap. That single instruction is the whole of
 * the path between the two halves of GEM, which is why so much of the AES
 * comes across unedited.
 *
 * Here both halves are host code, so the trap is a call. Nothing is copied,
 * because there is nothing to copy between: the arrays the AES filled in are
 * the arrays the VDI reads.
 *
 * An application's VDI call still goes the long way round, out through trap #2
 * and back in through vdi.c, because its arrays are in the emulated machine's
 * memory. The AES's do not have to.
 */

#include "emutos.h"
#include "gsx2.h"
#include "obdefs.h"
#include "gsxdefs.h"

#include "emuvdi.h"

VDIPB vdipb;

void gsx2(void)
{
    vdipb.contrl = contrl;

    emuvdi_call(vdipb.contrl, vdipb.intin, vdipb.ptsin,
                vdipb.intout, vdipb.ptsout);
}
