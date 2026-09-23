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
#define LINEA_V_CEL_HT  (-46)  /* the console font's height */
#define LINEA_V_CEL_MX  (-44)  /* columns on the screen, less one */
#define LINEA_V_CEL_MY  (-42)  /* and rows */
#define LINEA_V_CEL_WR  (-40)  /* bytes from one row of characters to the next */
#define LINEA_V_FNT_AD  (-22)  /* the console font's raster */
#define LINEA_V_FNT_ND  (-18)  /* its last character */
#define LINEA_V_FNT_ST  (-16)  /* and its first */
#define LINEA_V_FNT_WR  (-14)  /* bytes across its raster */
#define LINEA_V_REZ_HZ  (-12)  /* the screen's width in pixels */
#define LINEA_V_OFF_AD  (-10)  /* the console font's offset table */
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

/* The two of them that are about the mouse pointer rather than about drawing,
 * and which are answered by doing nothing - see m68k_linea */
#define LINEA_SHOW_MOUSE  (9)
#define LINEA_HIDE_MOUSE (10)

/* A font header as the machine lays one out, which is struct font_head in
 * 3rdparty/emutos/include/fonthdr.h with 68000 pointers in it */
#define FONT_ID          (0)
#define FONT_POINT       (2)
#define FONT_NAME        (4)   /* thirty two bytes */
#define FONT_FIRST_ADE  (36)   /* and fifteen more words after it */
#define FONT_HOR_TABLE  (68)
#define FONT_OFF_TABLE  (72)
#define FONT_DAT_TABLE  (76)
#define FONT_FORM_WIDTH (80)
#define FONT_FORM_HEIGHT (82)
#define FONT_NEXT_FONT  (84)
#define FONT_HEADER     (90)

#define FONT_NAME_LENGTH (32)

/* The three system fonts, as line-A lists them */
#define LINEA_FONTS      (3)

#endif /* LINEA_P_H */
