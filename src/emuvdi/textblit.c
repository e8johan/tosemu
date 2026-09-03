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
 * normal_blit, the inner loop that puts a character on the screen.
 *
 * This is the one piece of the VDI that EmuTOS has only in assembly. There is
 * vdi_tblit.S and a ColdFire variant of it, and no C behind either, so unlike
 * everything else in emuvdi/ this is not an adaptation of EmuTOS code but a
 * replacement for it, written against the same LOCALVARS structure that
 * vdi_textblit.c fills in.
 *
 * The assembly is a fast routine: it works a word at a time, with fringe masks
 * at each end, a rotate to line the source up with the destination, and four
 * variants of the loop chosen by how many words are involved. None of that
 * machinery is reproduced here. What is reproduced is what it computes.
 *
 * This works a pixel at a time instead, which is slower by a large factor and
 * does not matter at all: a character cell is eight by sixteen, so a glyph is
 * a hundred and twenty eight pixels across four planes, and the whole point of
 * the word-at-a-time machinery was to save cycles a host does not need to
 * count. What is bought with them is that the fringe masks, the rotate
 * direction, the word counts and the loop variants all stop existing, and with
 * them the ways of getting those wrong.
 *
 * The one thing that has to keep the original's shape is the special effects.
 * The masks for lighten and skew rotate as the blit walks the glyph, so they
 * advance once per source word rather than once per pixel, and doing that per
 * pixel would give a different pattern rather than a slower one.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"

/*
 * The same structure vdi_textblit.c declares. It is a copy because it is
 * private to that file, and it is the interface between the two, so it must
 * follow any change made there - which is exactly the warning the original
 * carries about the assembly.
 */
typedef struct {
    WORD unused5;
    WORD blt_flag;
    WORD unused1;
    WORD unused6;
    WORD unused7;
    WORD unused8;
    WORD unused9;
    WORD unused10;
    WORD unused11;
    WORD DESTY;
    WORD DELY;
    WORD DESTX;
    WORD DELX;
    WORD unused3;
    WORD WRT_MODE;
    WORD STYLE;
    WORD swap_tmps;
    WORD tmp_dely;
    WORD tmp_delx;
    WORD nextwrd;
    WORD nbrplane;
    WORD forecol;
    WORD thknover;
    WORD skew_msk;
    WORD lite_msk;
    WORD ambient;
    WORD smear;
    void *litejpw;
    void *thknjpw;
    void *litejpwf;
    void *thknjpwf;
    void *skewjmp;
    void *litejmp;
    void *thknjmp;
    WORD wrd_cnt;
    WORD shif_cnt;
    WORD rota_msk;
    WORD left_msk;
    WORD rite_msk;
    WORD thk_msk;
    WORD src_wthk;
    WORD src_wrd;
    WORD dest_wrd;
    WORD tddad;
    WORD tsdad;
    WORD height;
    WORD width;
    WORD d_next;
    WORD s_next;
    UBYTE *dform;
    UBYTE *sform;
    WORD unused2;
    WORD unused4;
    WORD buffa;
} LOCALVARS;

/*
 * The style bits are F_THICKEN, F_LIGHT and F_SKEW from vdi_defs.h. They are
 * used by name rather than copied: the copy is what the assembly had to do,
 * and getting one of them wrong shows up as the wrong effect rather than as a
 * build failure.
 */

/*
 * Which raster operation to use, by writing mode and by whether this plane
 * has a bit set in the foreground and the background colour.
 *
 * Modes 0 to 3 are the VDI ones, replace, transparent, exclusive or and
 * reverse transparent. From 4 up they are the raster operations themselves,
 * as vro_cpyfm numbers them, which is how the VDI writing mode ends up being
 * able to name all sixteen. Taken from wrmappin in vdi_tblit.S.
 */
#define OPS_PER_MODE (4)
#define WRT_MODES    (20)

static const UBYTE wrmappin[WRT_MODES][OPS_PER_MODE] = {
    /* fore/back  00  01  10  11 */
    {  0,  0,  3,  3 },     /* replace */
    {  4,  4,  7,  7 },     /* transparent */
    {  6,  6,  6,  6 },     /* exclusive or */
    {  1,  1, 13, 13 },     /* reverse transparent */

    {  0, 15,  0, 15 },     /* all zeros */
    {  0, 14,  1, 15 },     /* source and destination */
    {  0, 13,  2, 15 },     /* source and not destination */
    {  0, 12,  3, 15 },     /* source */
    {  0, 11,  4, 15 },     /* not source and destination */
    {  0, 10,  5, 15 },     /* destination */
    {  0,  9,  6, 15 },     /* source xor destination */
    {  0,  8,  7, 15 },     /* source or destination */
    {  0,  7,  8, 15 },     /* not (source or destination) */
    {  0,  6,  9, 15 },     /* not (source xor destination) */
    {  0,  5, 10, 15 },     /* not destination */
    {  0,  4, 11, 15 },     /* source or not destination */
    {  0,  3, 12, 15 },     /* not source */
    {  0,  2, 13, 15 },     /* not source or destination */
    {  0,  1, 14, 15 },     /* not (source and destination) */
    {  0,  0, 15, 15 }      /* all ones */
};

/* The sixteen raster operations, on one bit of source and one of destination */
static int raster_op(int op, int s, int d)
{
    switch (op)
    {
        case  0: return 0;
        case  1: return s & d;
        case  2: return s & !d;
        case  3: return s;
        case  4: return !s & d;
        case  5: return d;
        case  6: return s ^ d;
        case  7: return s | d;
        case  8: return !(s | d);
        case  9: return !(s ^ d);
        case 10: return !d;
        case 11: return s | !d;
        case 12: return !s;
        case 13: return !s | d;
        case 14: return !(s & d);
        case 15: return 1;
    }

    return d;
}

/*
 * A row of the source glyph, widened by the effects, as a run of bits.
 *
 * The source is one plane, so a row is just bits. Rather than carry the
 * rotates and masks of the original, a row is unpacked into one bit per entry,
 * which is what makes the rest of this readable.
 */
#define MAX_ROW_PIXELS (256)

static void source_row(const LOCALVARS *v, const UWORD *row, int shift,
                       UWORD lite, UBYTE *out, int width)
{
    int c, i;

    /*
     * How much of the source is this character.
     *
     * A font is a strip: the glyphs sit side by side in the same rows, so the
     * bits either side of one are the neighbouring characters rather than
     * blank. Anything read outside this span comes back empty, which is what
     * the fringe masks in the assembly were for. Skew is what makes it matter:
     * it reads to the left of the glyph, and without this the lean is drawn
     * out of whichever character happens to precede it.
     *
     * Thickening widens the destination rather than the source, and is done
     * below by smearing what was read, so its extra columns are not part of
     * the span.
     */
    int span = width - v->smear;

    for (c = 0; c < width; c++)
    {
        int col = c - shift;
        int bit = v->tsdad + col;
        int value = 0;

        if (col >= 0 && col < span)
        {
            UWORD word = row[bit >> 4];

            /*
             * Lighten screens the glyph with a mask that rotates as the blit
             * walks it, which is what makes the pattern a texture rather than
             * stripes. The mask is against the source word, so a pixel keeps
             * the bit of the mask at its own position within that word.
             */
            if (v->STYLE & F_LIGHT)
            {
                UWORD m = lite;
                int turns = bit >> 4;

                while (turns-- > 0)
                    rolw1(m);

                word &= m;
            }

            value = (word >> (15 - (bit & 15))) & 1;
        }

        out[c] = (UBYTE)value;
    }

    /*
     * Thicken smears the glyph rightwards. The caller has already widened
     * width by smear, so the extra columns are there to be filled: a pixel
     * lights every column from its own up to smear beyond it.
     */
    if (v->STYLE & F_THICKEN)
    {
        for (c = width - 1; c >= 0; c--)
        {
            if (out[c])
                continue;

            for (i = 1; i <= v->smear; i++)
            {
                if (c - i >= 0 && out[c - i])
                {
                    out[c] = 1;
                    break;
                }
            }
        }
    }
}

/*
 * normal_blit - copy a glyph into the destination, one plane at a time
 *
 * Called with a pointer past the end of the structure rather than to the start
 * of it, because the assembly this replaces addressed its fields as negative
 * offsets from the end of a stack frame.
 *
 * src and dst are the bottom row of the glyph and of where it goes, and
 * s_next and d_next are negative, so the walk is upwards. That is the caller's
 * arrangement and is left alone.
 */
void normal_blit(LOCALVARS *vars_end, UBYTE *src, UBYTE *dst)
{
    LOCALVARS *v = vars_end - 1;
    UBYTE row[MAX_ROW_PIXELS];
    int plane, mode, width;

    width = v->width;
    if (width > MAX_ROW_PIXELS)
        width = MAX_ROW_PIXELS;

    mode = v->WRT_MODE;
    if (mode < 0 || mode >= WRT_MODES)
        mode = 0;

    for (plane = 0; plane < v->nbrplane; plane++)
    {
        int fore = (v->forecol >> plane) & 1;
        int back = (v->ambient >> plane) & 1;
        int op = wrmappin[mode][(fore << 1) | back];
        /*
         * The masks come from the globals rather than from the structure.
         * vdi_textblit.c never writes these fields: the assembly reloads
         * LITEMASK and SKEWMASK itself at the top of each character, so the
         * fields hold whatever the last character left in them.
         */
        UWORD lite = LITEMASK;
        UWORD skew = SKEWMASK;
        int shift = 0;
        int r;

        for (r = 0; r < v->height; r++)
        {
            const UWORD *srow = (const UWORD *)(src + r * v->s_next);
            UBYTE *drow = dst + r * v->d_next + plane * (int)sizeof(WORD);
            int c;

            /*
             * Skew leans the glyph over. The mask says, one bit to a row,
             * where the lean steps across by a pixel, and it is rotated
             * rather than indexed so that the same mask serves any height.
             */
            if (v->STYLE & F_SKEW)
            {
                UWORD before = skew;

                rolw1(skew);
                v->skew_msk = skew;     /* rotate() reads this back */
                if (before & 0x8000)
                    shift++;
            }

            source_row(v, srow, shift, lite, row, width);

            for (c = 0; c < width; c++)
            {
                int bit = v->tddad + c;
                UWORD *word = (UWORD *)(drow + (bit >> 4) * v->nextwrd);
                int mask = 0x8000 >> (bit & 15);
                int s = row[c];
                int d = (*word & mask) ? 1 : 0;

                if (raster_op(op, s, d))
                    *word |= mask;
                else
                    *word &= ~mask;
            }

            /* The lighten mask advances a step for every source word the row
             * covered, which is what the original does as it walks them */
            if (v->STYLE & F_LIGHT)
            {
                int words = ((v->tsdad + width - 1) >> 4) + 1;

                while (words-- > 0)
                    rolw1(lite);
            }
        }
    }
}
