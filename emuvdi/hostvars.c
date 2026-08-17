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

/* The globals the EmuTOS VDI draws through, defined for a hosted build.
 *
 * EmuTOS puts these in bios/lineavars.S, in assembly, because line-A requires
 * them at fixed offsets from one another. Nothing here reaches them through
 * line-A, so they are ordinary C.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"

#include <string.h>

/* Describing the surface being drawn into */
UWORD v_planes;
UWORD v_lin_wr;
UBYTE *v_bas_ad;
UWORD V_REZ_HZ;
UWORD V_REZ_VT;
UWORD BYTES_LIN;

/*
 * log2(8/v_planes): turns a pixel column into a byte offset, as
 * (x & 0xfff0) >> v_planes_shift. EmuTOS computes it in bios/lineainit.c.
 */
UBYTE v_planes_shift;

/* The workstation tables an open workstation reports */
WORD DEV_TAB[45];
WORD SIZ_TAB[45];
WORD INQ_TAB[45];

/* The parameter arrays. In a hosted VDI these point at the arrays the trap
 * unpacked rather than at the ones a resident VDI owns. */
WORD *CONTRL, *INTIN, *PTSIN, *INTOUT, *PTSOUT;

/* Drawing state that the line-A entry points share with the VDI */
WORD WRT_MODE;
WORD CLIP;
WORD XMINCL, XMAXCL, YMINCL, YMAXCL;
WORD X1, Y1, X2, Y2;
WORD COLBIT0, COLBIT1, COLBIT2, COLBIT3;
UWORD *PATPTR;
UWORD PATMSK;
WORD MFILL;
WORD COPYTRAN;
WORD LN_MASK, LSTLIN;
WORD TERM_CH;
WORD flip_y;
WORD line_cw;
WORD num_qc_lines;
WORD val_mode, chc_mode, loc_mode, str_mode;

/* Text state */
WORD XDDA;
UWORD DDAINC;
WORD SCALDIR;
WORD MONO;
WORD SOURCEX, SOURCEY;
WORD DESTX, DESTY;
UWORD DELX, DELY;
const UWORD *FBASE;
WORD FWIDTH;
WORD STYLE;
WORD LITEMASK, SKEWMASK;
WORD WEIGHT;
WORD ROFF, LOFF;
WORD SCALE;
WORD CHUP;
WORD TEXTFG;
WORD *SCRTCHP;
WORD SCRPT2;
WORD TEXTBG;
WORD COPYTRAN_TEXT;

/* Mouse state, which a hosted VDI never moves */
WORD GCURX, GCURY;
WORD HIDE_CNT;
WORD MOUSE_BT;
WORD newx, newy;
UBYTE draw_flag, mouse_flag, cur_ms_stat;

/* The colours that were asked for, in tenths of a percent, which is what
 * vq_color reports back rather than what the hardware rounded them to.
 *
 * MAP_COL and REV_MAP_COL, which map a colour index onto a hardware pen, are
 * not here: vdi_col.c defines those itself.
 */
WORD REQ_COL[16][3];

/*
 * Whether the machine has an STE shifter, which is what decides between the
 * three bits per gun of an ST and the four of an STE. A surface has as many
 * as we say it has, and there is no reason to be the poorer one.
 */
int has_ste_shifter = 1;

/* The workstation the line-A flood fill borrows, and the hook that lets a
 * caller stop it early */
Vwk *CUR_WORK;
WORD (*SEEDABORT)(void);

/*
 * From vdi_control.c, which is replaced wholesale because it is the trap
 * entry. This one function is wanted on its own, so it is here rather than
 * dragging the file in.
 */
WORD validate_color_index(WORD colnum)
{
    if ((colnum < 0) || (colnum >= numcolors))
        return 1;

    return colnum;
}

/*
 * The timer vector the VDI chains onto, and the one it saved. EmuTOS defines
 * these in assembly because they are interrupt entry points. Nothing here
 * interrupts, so vex_timv keeps a vector that is never taken.
 */
ETV_TIMER_T tim_chain;
ETV_TIMER_T tim_addr;

/*
 * The palette. On a real machine these are XBIOS calls that write shifter
 * registers; here they are the surface's palette, which the presenter reads
 * when it turns plane words into colours.
 *
 * An entry is 0x0RGB with four bits a gun, the STE layout, because
 * has_ste_shifter above says so.
 */
static UWORD palette[256];

WORD Setcolor(WORD colornum, WORD color)
{
    WORD old;

    if (colornum < 0 || colornum >= (WORD)(sizeof palette / sizeof palette[0]))
        return 0;

    old = palette[colornum];

    /* A negative colour is a read rather than a write */
    if (color >= 0)
        palette[colornum] = color;

    return old;
}

WORD EsetColor(WORD colornum, WORD color)
{
    return Setcolor(colornum, color);
}

void VsetRGB(WORD index, WORD count, LONG rgb)
{
    (void)index;
    (void)count;
    (void)rgb;
}

void VgetRGB(WORD index, WORD count, LONG rgb)
{
    (void)index;
    (void)count;
    (void)rgb;
}

/*
 * EmuTOS names its own bzero so that the compiler does not turn a call to the
 * standard one into a builtin that calls it back again. Here it can simply be
 * the standard one.
 */
void bzero_nobuiltin(void *addr, ULONG size)
{
    memset(addr, 0, size);
}

/* A vector that is never taken, on a machine where nothing interrupts */
LONG host_setexc(WORD vecnum, LONG vec)
{
    (void)vecnum;
    (void)vec;

    return 0;
}

void just_rts(void)
{
}

/*
 * Sets the globals above to describe a surface, the way EmuTOS's
 * set_screen_shift and the line-A init do for the screen.
 */
void host_surface_select(void *base, UWORD width, UWORD height, UWORD planes)
{
    static const UBYTE shift_offset[9] = { 0, 3, 2, 0, 1, 0, 0, 0, 0 };

    v_bas_ad = base;
    v_planes = planes;
    v_lin_wr = width / 16 * planes * 2;
    v_planes_shift = (planes > 4) ? 0 : shift_offset[planes];

    V_REZ_HZ = width;
    V_REZ_VT = height;
    BYTES_LIN = v_lin_wr;

    xres = width - 1;
    yres = height - 1;
    numcolors = 1 << planes;
}
