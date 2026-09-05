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
 * Reading a .FNT file.
 *
 * A font file is a header of eighty eight bytes, then up to three tables: the
 * horizontal offsets, which most fonts do not have; the character offsets,
 * which say where in the raster each character starts and therefore how wide
 * it is; and the raster itself, which is every character of the font side by
 * side in one long strip. The three pointers in the header are offsets into
 * the file, and become addresses in this program on the way in.
 *
 * There are two byte orders. The files Atari shipped are Intel, having been
 * made on a PC, and the flags word says which a file is: F_STDFORM means the
 * raster is in Motorola order and by extension that the rest of it is too.
 * The flags word is itself a word, so it has to be read before its own byte
 * order is known - it is read big endian, which works either way, because
 * every value the flag can take is small enough that the byte carrying it is
 * the same byte in both readings.
 *
 * The raster is the one thing left as it was found. vdi_vst_load_fonts walks
 * the chain it is given and byte swaps anything without F_STDFORM, setting the
 * flag as it goes, using trnsfont - which is private to vdi_text.c and cannot
 * be reached from here. Doing it here as well would mean writing that routine
 * a second time and marking the font so that the first one skips it, which is
 * two ways of doing one thing. So the raster is handed over as the file had
 * it, which is what the VDI is expecting.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"

#include <stdio.h>
#include <stdlib.h>

#include "gdos.h"
#include "emuvdi.h"

/*
 * Where each field of the header is, in bytes from the start of the file. The
 * structure in fonthdr.h says the same thing, but only for a compiler whose
 * WORD is two bytes and whose pointer is four, and this one's is eight - so
 * the file is read by offset rather than by being cast.
 */
#define FNT_HEADER      (88)

#define FNT_FONT_ID      (0)
#define FNT_POINT        (2)
#define FNT_NAME         (4)
#define FNT_FIRST_ADE   (36)
#define FNT_LAST_ADE    (38)
#define FNT_TOP         (40)
#define FNT_ASCENT      (42)
#define FNT_HALF        (44)
#define FNT_DESCENT     (46)
#define FNT_BOTTOM      (48)
#define FNT_MAX_CHAR_W  (50)
#define FNT_MAX_CELL_W  (52)
#define FNT_LEFT_OFF    (54)
#define FNT_RIGHT_OFF   (56)
#define FNT_THICKEN     (58)
#define FNT_UL_SIZE     (60)
#define FNT_LIGHTEN     (62)
#define FNT_SKEW        (64)
#define FNT_FLAGS       (66)
#define FNT_HOR_TABLE   (68)
#define FNT_OFF_TABLE   (72)
#define FNT_DAT_TABLE   (76)
#define FNT_FORM_WIDTH  (80)
#define FNT_FORM_HEIGHT (82)

/* Reading the file's words and longs, in whichever order it turned out to be
 * written in. Motorola is the 68000's, which is what F_STDFORM says. */
static UWORD word_at(const UBYTE *file, long at, int motorola)
{
    if (motorola)
        return (UWORD)((file[at] << 8) | file[at + 1]);

    return (UWORD)((file[at + 1] << 8) | file[at]);
}

static ULONG long_at(const UBYTE *file, long at, int motorola)
{
    if (motorola)
        return ((ULONG)word_at(file, at, 1) << 16) | word_at(file, at + 2, 1);

    return ((ULONG)word_at(file, at + 2, 0) << 16) | word_at(file, at, 0);
}

/* The whole of a file, or null. Fonts are tens of kilobytes at the most, so
 * there is nothing to be gained by reading one in pieces. */
static UBYTE *file_read(const char *host_path, long *size)
{
    FILE *f = fopen(host_path, "rb");
    UBYTE *data;
    long length;

    if (!f)
        return 0;

    if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0)
    {
        fclose(f);
        return 0;
    }

    rewind(f);

    data = malloc((size_t)length);
    if (!data)
    {
        fclose(f);
        return 0;
    }

    if (fread(data, 1, (size_t)length, f) != (size_t)length)
    {
        free(data);
        fclose(f);
        return 0;
    }

    fclose(f);
    *size = length;

    return data;
}

static void refuse(const char *host_path, const char *why)
{
    fprintf(stderr, "tosemu: %s is not a font tosemu can read: %s\n",
            host_path, why);
}

/*
 * Copying a table out of the file.
 *
 * The three tables are allocated with host_vdi_alloc rather than malloc so
 * that they sit below the four gigabyte line, along with the header that
 * points at them - the head of the chain travels to the VDI through two words
 * of the control array, and a font whose tables were higher up would be
 * reached through pointers the header cannot hold either, an Atari font header
 * being what it is.
 */
static void *table_alloc(long bytes)
{
    return host_vdi_alloc(bytes);
}

Fonthead *gdos_font_read(const char *host_path)
{
    UBYTE *file;
    long size = 0;
    Fonthead *font;
    UWORD first, last, form_width, form_height, flags;
    ULONG hor_at, off_at, dat_at;
    long chars, off_bytes, raster_bytes;
    int motorola, i;
    UWORD *off_table;
    UBYTE *hor_table = 0, *dat_table;

    file = file_read(host_path, &size);
    if (!file)
    {
        refuse(host_path, "it could not be read");
        return 0;
    }

    if (size < FNT_HEADER)
    {
        refuse(host_path, "it is shorter than a font header");
        free(file);
        return 0;
    }

    /* Which way round it is written. F_STDFORM lives in the low byte of the
     * flags word either way, so a big endian read finds it in both. */
    motorola = (word_at(file, FNT_FLAGS, 1) & F_STDFORM) ? 1 : 0;

    flags = word_at(file, FNT_FLAGS, motorola);
    first = word_at(file, FNT_FIRST_ADE, motorola);
    last = word_at(file, FNT_LAST_ADE, motorola);
    form_width = word_at(file, FNT_FORM_WIDTH, motorola);
    form_height = word_at(file, FNT_FORM_HEIGHT, motorola);

    hor_at = long_at(file, FNT_HOR_TABLE, motorola);
    off_at = long_at(file, FNT_OFF_TABLE, motorola);
    dat_at = long_at(file, FNT_DAT_TABLE, motorola);

    if (first > last || last > 255)
    {
        refuse(host_path, "it does not say which characters it has");
        free(file);
        return 0;
    }

    chars = (long)last - first + 1;
    off_bytes = (chars + 1) * 2;    /* one more, for where the last one ends */
    raster_bytes = (long)form_width * form_height;

    /*
     * A raster whose rows are an odd number of bytes cannot be blitted: every
     * routine that draws a character works a word at a time, and so does the
     * byte swapping the VDI is about to do to this one.
     */
    if (form_width == 0 || (form_width & 1) || form_height == 0)
    {
        refuse(host_path, "its raster is not a whole number of words across");
        free(file);
        return 0;
    }

    if (off_at + off_bytes > (ULONG)size || dat_at + raster_bytes > (ULONG)size
        || ((flags & F_HORZ_OFF) && hor_at + chars * 2 > (ULONG)size))
    {
        refuse(host_path, "one of its tables runs off the end of the file");
        free(file);
        return 0;
    }

    font = host_vdi_alloc((long)sizeof *font);
    off_table = table_alloc(off_bytes);
    dat_table = table_alloc(raster_bytes);
    if (flags & F_HORZ_OFF)
        hor_table = table_alloc(chars * 2);

    if (!font || !off_table || !dat_table
        || ((flags & F_HORZ_OFF) && !hor_table))
    {
        refuse(host_path, "there was no room for it");
        host_vdi_free(font);
        host_vdi_free(off_table);
        host_vdi_free(dat_table);
        host_vdi_free(hor_table);
        free(file);
        return 0;
    }

    font->font_id = (WORD)word_at(file, FNT_FONT_ID, motorola);
    font->point = (WORD)word_at(file, FNT_POINT, motorola);

    /* The name is bytes rather than words, so it needs no turning over. It is
     * not required to be terminated, hence the last byte being set here. */
    for (i = 0; i < FONT_NAME_LEN; i++)
        font->name[i] = (char)file[FNT_NAME + i];
    font->name[FONT_NAME_LEN - 1] = 0;

    font->first_ade = first;
    font->last_ade = last;
    font->top = word_at(file, FNT_TOP, motorola);
    font->ascent = word_at(file, FNT_ASCENT, motorola);
    font->half = word_at(file, FNT_HALF, motorola);
    font->descent = word_at(file, FNT_DESCENT, motorola);
    font->bottom = word_at(file, FNT_BOTTOM, motorola);
    font->max_char_width = word_at(file, FNT_MAX_CHAR_W, motorola);
    font->max_cell_width = word_at(file, FNT_MAX_CELL_W, motorola);
    font->left_offset = word_at(file, FNT_LEFT_OFF, motorola);
    font->right_offset = word_at(file, FNT_RIGHT_OFF, motorola);
    font->thicken = word_at(file, FNT_THICKEN, motorola);
    font->ul_size = word_at(file, FNT_UL_SIZE, motorola);
    font->lighten = word_at(file, FNT_LIGHTEN, motorola);
    font->skew = word_at(file, FNT_SKEW, motorola);
    font->flags = flags;
    font->form_width = form_width;
    font->form_height = form_height;
    font->reserved = 0;
    font->next_font = NULL;     /* gdos_font_chain decides what follows */

    for (i = 0; i <= (int)chars; i++)
        off_table[i] = word_at(file, (long)off_at + i * 2, motorola);

    if (hor_table)
        for (i = 0; i < (int)chars * 2; i++)
            hor_table[i] = file[hor_at + i];

    /* Verbatim, and F_STDFORM left as the file set it, so that the VDI does
     * the swapping with the routine it already has */
    for (i = 0; i < (int)raster_bytes; i++)
        dat_table[i] = file[dat_at + i];

    font->off_table = off_table;
    font->hor_table = hor_table;
    font->dat_table = (const UWORD *)dat_table;

    free(file);

    return font;
}

void gdos_font_free(Fonthead *font)
{
    if (!font)
        return;

    host_vdi_free((void *)font->off_table);
    host_vdi_free((void *)font->hor_table);
    host_vdi_free((void *)font->dat_table);
    host_vdi_free(font);
}

/* By face, and within a face by size. See the header for why it matters. */
static int by_face_then_size(const void *a, const void *b)
{
    const Fonthead *fa = *(const Fonthead * const *)a;
    const Fonthead *fb = *(const Fonthead * const *)b;

    if (fa->font_id != fb->font_id)
        return fa->font_id < fb->font_id ? -1 : 1;

    if (fa->point != fb->point)
        return fa->point < fb->point ? -1 : 1;

    /* Two fonts of one face at one point size differ in how tall they are
     * drawn, which is what a screen with tall pixels asks for */
    if (fa->top != fb->top)
        return fa->top < fb->top ? -1 : 1;

    return 0;
}

Fonthead *gdos_font_chain(Fonthead **font, int count)
{
    int i;

    if (count <= 0)
        return 0;

    qsort(font, (size_t)count, sizeof *font, by_face_then_size);

    for (i = 0; i < count - 1; i++)
        font[i]->next_font = font[i + 1];

    font[count - 1]->next_font = NULL;

    return font[0];
}

/*
 * The scratch buffer arithmetic, which is vdi_text.c's done over again for a
 * font it did not know about.
 *
 * The original is a page of #defines at the top of that file, worked out at
 * compile time for the 8x16 system font, and the names below are its names so
 * that the two can be read side by side. What it comes to: a cell may be
 * turned on its side, so its width and height both have to be counted as
 * either; it may be doubled, when a size is asked for that no font has; and
 * outlining adds a pixel all round, whichever buffer it happens in. The
 * largest of those is what has to fit.
 */
WORD gdos_font_scratch(const Fonthead *font)
{
    long total_off = (long)font->left_offset + font->right_offset;
    long mxcelwd = font->max_cell_width;
    long form_ht = font->form_height;
    long cel2_ww, cel2_wh, cel2_hh, cel2_hw, cel2_siz, out_add;

    cel2_ww = (((2 * (total_off + mxcelwd)) + 3 + 15) / 16) * 2;
    cel2_wh = (2 * (total_off + mxcelwd)) + 2;
    cel2_hh = (2 * form_ht) + 2;
    cel2_hw = (((2 * form_ht) + 3 + 15) / 16) * 2;

    cel2_siz = cel2_ww * cel2_hh;
    if (cel2_wh * cel2_hw > cel2_siz)
        cel2_siz = cel2_wh * cel2_hw;

    out_add = (cel2_ww >= cel2_hw ? cel2_ww : cel2_hw) + 2;

    return (WORD)(cel2_siz + out_add);
}
