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
 * Putting characters in cells, which is the half of the console that knows
 * what a screen is made of.
 *
 * The VT52 above it - which escape means what, where the cursor goes, when the
 * screen scrolls - is EmuTOS's bios/vt52.c, compiled out of the submodule and
 * unedited. This is bios/conout.c, and it is the second file here that
 * replaces EmuTOS code rather than adapting it, for the same reason as the
 * first: byte order.
 *
 * The original addresses a cell as a byte. A word of a plane holds sixteen
 * pixels, which is two cells of an eight wide font, so an even cell is the
 * first byte of the word and an odd one the second - on a machine where the
 * first byte of a word is its top half. Surfaces here are host endian, which
 * is what lets the rest of the VDI go unedited, so the two halves are the
 * other way round and every pair of characters would come out swapped. The
 * same trap CONF_WITH_VDI_TEXT_SPEEDUP is turned off for; see the README.
 *
 * So this works in words and masks off the half it wants, which is right
 * either way round. Everything else about it - what the four combinations of
 * foreground and background bit do to a plane, how the cursor is drawn by
 * inverting the cell it sits on, what a scroll leaves behind - is EmuTOS's
 * arrangement, kept because vt52.c above is written to it.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"
#include "conout.h"

#include <string.h>

/*
 * The console's own line-A variables.
 *
 * hostvars.c has the ones the VDI draws through, including the cell metrics,
 * because the VDI's escape functions keep track of where a text cursor would
 * be even though a hosted VDI has no console. These are the ones only a
 * console uses, so they live beside the code that uses them.
 */
UBYTE *v_cur_ad;                /* the cell the cursor is on */
WORD v_cur_of;                  /* how far down the screen the first row is */
UBYTE v_cur_tim;                /* ticks until the cursor next changes state */
UBYTE v_period;                 /* and how many there are between changes */
WORD disab_cnt;                 /* how deep the cursor is turned off */
UBYTE v_stat_0;                 /* the cursor and video state bits */
WORD sav_cur_x;                 /* where ESC j put the cursor */
WORD sav_cur_y;
WORD v_col_fg;                  /* the colours characters are drawn in */
WORD v_col_bg;

/* The font, which font_set_default in fonts.c chooses and fills in */
const UWORD *v_fnt_ad;
const UWORD *v_off_ad;
UWORD v_fnt_st;
UWORD v_fnt_nd;
UWORD v_fnt_wr;

/*
 * The three the console keeps in EmuTOS's system variable page rather than in
 * line-A. con_state is the VT52's state machine - which of its handlers reads
 * the next byte - and it is what vt52.c drives itself through.
 *
 * conterm is the keyboard and console settings byte. Bit 2 is what says
 * whether the bell rings, and nothing here sets any of the others, so it
 * starts as an ST's does: key click, key repeat and the bell all on.
 */
void (*con_state)(WORD);
WORD save_row;
UBYTE conterm = 0x07;

/*
 * How many characters have actually appeared on the console.
 *
 * This is not bookkeeping the console needs; it is what says whether there is
 * anything to look at. A program that writes an escape sequence and nothing
 * else has written to the console and said nothing, and opening a window to
 * show the result would be a window with nothing in it - see screen_show in
 * console.c, which is the only reader.
 */
ULONG host_console_written;

/*
 * The bell, which on an ST was the sound chip.
 *
 * There is no sound chip and there is not going to be one, so this is the
 * terminal's bell instead. It goes to stderr rather than to the console
 * because the console is a screen, and a character written on it would be a
 * character an application did not ask for.
 */
void bell(void)
{
    /* Nothing yet. A GEM application that rings it is asking for a noise
     * tosemu has nowhere to make, and printing something would be worse than
     * silence. */
}

/* How many words a row of the surface is, which is what v_lin_wr says in
 * bytes. Every screen is a whole number of words across - see surface.c - so
 * this divides exactly. */
static int words_per_line(void)
{
    return v_lin_wr / 2;
}

/* The first word of a cell, which is the pair of cells the cell is half of */
static UWORD *cell_word(int cx, int y)
{
    UWORD *base = (UWORD *)v_bas_ad;

    return base + (size_t)y * words_per_line() + (cx >> 1) * v_planes;
}

/* And which half of it this cell is. An even cell is the leftmost eight
 * pixels of the word, which is its top half whatever the host's byte order
 * does with the two. */
static int cell_shift(int cx)
{
    return IS_ODD(cx) ? 0 : 8;
}

/*
 * One row of one cell, given the eight bits of the glyph on that row.
 *
 * The four cases EmuTOS's cell_xfer names are here as arithmetic rather than
 * as branches: a plane where the foreground colour has a bit set takes the
 * glyph, a plane where the background has one takes its inverse, a plane with
 * both takes all ones and a plane with neither takes all noughts.
 */
static void row_put(int cx, int y, UBYTE bits, UWORD fg, UWORD bg)
{
    UWORD *word = cell_word(cx, y);
    int shift = cell_shift(cx);
    UWORD mask = (UWORD)(0x00ffu << shift);
    int plane;

    for (plane = 0; plane < v_planes; plane++)
    {
        UBYTE value = (UBYTE)((((fg >> plane) & 1) ? bits : 0)
                              | (((bg >> plane) & 1) ? (UBYTE)~bits : 0));

        word[plane] = (UWORD)((word[plane] & ~mask)
                              | (((UWORD)value << shift) & mask));
    }
}

/* And the same row inverted, which is how the cursor is drawn: a second
 * inversion puts back what was underneath, so nothing has to be remembered */
static void row_invert(int cx, int y)
{
    UWORD *word = cell_word(cx, y);
    UWORD mask = (UWORD)(0x00ffu << cell_shift(cx));
    int plane;

    for (plane = 0; plane < v_planes; plane++)
        word[plane] ^= mask;
}

/*
 * The eight bits of a character on one row of its cell.
 *
 * A font is one long strip of glyphs rather than a glyph at a time, so a
 * character is found by where along the row it starts. The offset table says
 * that in pixels, and the strip is read a word at a time - as the VDI reads
 * it - so which half of the word holds the character is the same question as
 * for a cell, and has the same answer.
 */
static UBYTE glyph_row(WORD ch, int row)
{
    UWORD offset;
    UWORD word;

    if (ch < v_fnt_st || ch > v_fnt_nd || !v_fnt_ad || !v_off_ad)
        return 0;

    offset = v_off_ad[ch];

    word = v_fnt_ad[row * (v_fnt_wr / 2) + (offset >> 4)];

    return (UBYTE)((offset & 8) ? (word & 0xff) : (word >> 8));
}

void invert_cell(int x, int y)
{
    int row;

    if (x < 0 || y < 0 || x > v_cel_mx || y > v_cel_my)
        return;

    for (row = 0; row < v_cel_ht; row++)
        row_invert(x, y * v_cel_ht + row);
}

/*
 * Where the cursor is being shown, which is not always where it is.
 *
 * move_cursor updates the coordinates first and then erases the cursor from
 * where it was, so the old place has to be remembered somewhere. EmuTOS keeps
 * it in v_cur_ad, which is an address into the screen; the addressing here is
 * done from coordinates, so these are the coordinates. v_cur_ad is kept
 * pointing at the same cell for anything that reads it.
 */
static WORD shown_cx;
static WORD shown_cy;

static void cursor_shown_at(int x, int y)
{
    shown_cx = x;
    shown_cy = y;

    v_cur_ad = (UBYTE *)cell_word(x, y * v_cel_ht);
}

void move_cursor(int x, int y)
{
    if (x < 0)
        x = 0;
    else if (x > v_cel_mx)
        x = v_cel_mx;

    if (y < 0)
        y = 0;
    else if (y > v_cel_my)
        y = v_cel_my;

    v_cur_cx = x;
    v_cur_cy = y;

    /* Nothing to move if nothing is being shown */
    if (!(v_stat_0 & M_CVIS))
    {
        cursor_shown_at(x, y);
        return;
    }

    /* A cursor that flashes and happens to be in its off half is not on the
     * screen to be erased, so it is simply drawn in the new place */
    if (v_stat_0 & M_CFLASH)
    {
        v_stat_0 &= ~M_CVIS;

        if (!(v_stat_0 & M_CSTATE))
        {
            cursor_shown_at(x, y);
            invert_cell(x, y);
            v_stat_0 |= M_CSTATE;
            v_cur_tim = v_period;
            v_stat_0 |= M_CVIS;
            return;
        }
    }

    invert_cell(shown_cx, shown_cy);
    cursor_shown_at(x, y);
    invert_cell(x, y);

    /* A cursor that has just moved is shown solidly for a moment, so that
     * something moving does not disappear as it arrives */
    v_cur_tim = v_period;

    v_stat_0 |= M_CVIS;
}

void ascii_out(int ch)
{
    UWORD fg, bg;
    int visible;
    int row;

    if (ch < v_fnt_st || ch > v_fnt_nd)
        return;

    /* Reverse video swaps the two, which is the whole of what it is */
    if (v_stat_0 & M_REVID)
    {
        fg = v_col_bg;
        bg = v_col_fg;
    }
    else
    {
        fg = v_col_fg;
        bg = v_col_bg;
    }

    visible = v_stat_0 & M_CVIS;
    if (visible)
        v_stat_0 &= ~M_CVIS;

    for (row = 0; row < v_cel_ht; row++)
        row_put(v_cur_cx, v_cur_cy * v_cel_ht + row, glyph_row(ch, row),
                fg, bg);

    host_console_written++;

    /*
     * Where the next character goes.
     *
     * A character written in the last column does not move the cursor unless
     * wrapping is on, which is what stops the screen scrolling every time
     * something fills the bottom row exactly.
     */
    if (v_cur_cx == v_cel_mx)
    {
        if (v_stat_0 & M_CEOL)
        {
            v_cur_cx = 0;

            if (v_cur_cy < v_cel_my)
                v_cur_cy++;
            else
                scroll_up(0);
        }
    }
    else
        v_cur_cx++;

    cursor_shown_at(v_cur_cx, v_cur_cy);

    if (visible)
    {
        invert_cell(v_cur_cx, v_cur_cy);
        v_stat_0 |= M_CSTATE;
        v_stat_0 |= M_CVIS;

        if (v_stat_0 & M_CFLASH)
            v_cur_tim = v_period;
    }
}

/*
 * Fills cells with the background colour, from one corner to another.
 *
 * EmuTOS requires the left column to be even and the right one odd, because
 * it works in whole words and says so in a comment. Nothing here does: a cell
 * is masked into its half of the word, so either end may be either. The
 * callers in vt52.c go on obeying the rule and the result is the same.
 */
void blank_out(int topx, int topy, int botx, int boty)
{
    int x, y, row;

    if (topx < 0)
        topx = 0;
    if (topy < 0)
        topy = 0;
    if (botx > v_cel_mx)
        botx = v_cel_mx;
    if (boty > v_cel_my)
        boty = v_cel_my;

    for (y = topy; y <= boty; y++)
        for (row = 0; row < v_cel_ht; row++)
            for (x = topx; x <= botx; x++)
                row_put(x, y * v_cel_ht + row, 0, 0, v_col_bg);
}

/*
 * Moving the screen up by a row, and down by one.
 *
 * These are whole rows of whole words, so they are a move of memory and the
 * byte order underneath it never comes into it. That is why they are EmuTOS's
 * code with only the pointer arithmetic spelled out.
 */
void scroll_up(UWORD top_line)
{
    UBYTE *dst = v_bas_ad + (ULONG)top_line * v_cel_wr;
    UBYTE *src = dst + v_cel_wr;
    ULONG count = (ULONG)v_cel_wr * (v_cel_my - top_line);

    memmove(dst, src, count);

    blank_out(0, v_cel_my, v_cel_mx, v_cel_my);
}

void scroll_down(UWORD start_line)
{
    UBYTE *src = v_bas_ad + (ULONG)start_line * v_cel_wr;
    UBYTE *dst = src + v_cel_wr;
    ULONG count = (ULONG)v_cel_wr * (v_cel_my - start_line);

    memmove(dst, src, count);

    blank_out(0, start_line, v_cel_mx, start_line);
}
