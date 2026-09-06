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

#ifndef LINEA_P_H
#define LINEA_P_H

#define LINEA_TRACE_CONTEXT
#include "config.h"

/*
 * The line-A variables, as offsets from the base address $a000 hands back.
 *
 * Half of them are below it, which is why the base is a pointer into the
 * middle of a block rather than the front of one. The offsets are Atari's and
 * are the same in 3rdparty/emutos/bios/lineavars.S, which is where the rest of
 * them are written down; these are the ones tosemu has an answer for.
 */
#define LINEA_V_REZ_HZ  (-12)  /* the screen's width in pixels */
#define LINEA_V_REZ_VT   (-4)  /* and its height */
#define LINEA_BYTES_LIN  (-2)  /* bytes from one line of it to the next */
#define LINEA_V_PLANES    (0)  /* how many planes it has */
#define LINEA_V_LIN_WR    (2)  /* the same as BYTES_LIN, under its other name */
#define LINEA_CONTRL      (4)  /* and the VDI parameter block the drawing */
#define LINEA_INTIN       (8)  /* routines take their arguments out of, which */
#define LINEA_PTSIN      (12)  /* is five pointers rather than five arrays */
#define LINEA_INTOUT     (16)
#define LINEA_PTSOUT     (20)

/* How far the block reaches either side of the base. EmuTOS's lowest variable
 * is at -910 and its highest of the documented ones is not far past +120; both
 * are rounded up, the block being small and the cost of being wrong about it
 * being an application writing into something else. */
#define LINEA_BELOW    (1024)
#define LINEA_ABOVE     (256)

/* The sixteen line-A opcodes, $a000 to $a00f. See the table in
 * 3rdparty/emutos/bios/linea.S for what each of them draws. */
#define LINEA_CALLS      (16)

#endif /* LINEA_P_H */
