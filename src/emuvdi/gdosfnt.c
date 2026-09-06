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
 * The raster is turned over here too, and F_STDFORM set, so that what comes
 * out is a font in the VDI's own order whatever order it went in in. EmuTOS
 * has that swap already, in trnsfont, and it is not reachable: it is static
 * inside vdi_text.c and the only thing that calls it is vdi_vst_load_fonts,
 * which tosemu cannot call - see gdos_install below for why.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>

/* EmuTOS's own, which comes first on the include path */
#include "string.h"

/* tosemu's, reached the way hostfs.c reaches them: this side is built against
 * EmuTOS's headers, so a tosemu header included here has to be one written in
 * plain types, which these are */
#include "../files.h"
#include "../settings.h"
#include "../tossystem.h"

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

/*
 * How much room is left at each end of the offset table for characters the
 * font does not have. A whole character set, because that is the furthest a
 * character code can fall outside the range in either direction.
 */
#define FNT_OFF_SLACK (256)

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
    off_table = table_alloc(off_bytes + 2 * FNT_OFF_SLACK * 2);
    dat_table = table_alloc(raster_bytes);
    if (flags & F_HORZ_OFF)
        hor_table = table_alloc(chars * 2);

    if (!font || !off_table || !dat_table
        || ((flags & F_HORZ_OFF) && !hor_table))
    {
        refuse(host_path, "there was no room for it");
        host_vdi_free(font);
        host_vdi_free(off_table);     /* still the base, not yet moved on */
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

    /*
     * The offset table, with the same value repeated either side of it.
     *
     * The VDI reads a character's width as off_table[c - first_ade + 1] minus
     * off_table[c - first_ade], and does not check that the character is one
     * the font has. For the fonts compiled in that hardly matters, they cover
     * everything from a space up; for a font off a disk it matters a great
     * deal, because the range is whatever somebody put in the file - and a
     * document with a tab in it, or a character above the range, then reads
     * outside the table entirely.
     *
     * Rather than let that be memory nobody owns, the table is allocated with
     * a character set of room at each end and the pointer handed out points at
     * the middle of it. The padding repeats the first and last offsets, so a
     * character the font does not have measures nought - which is what a
     * character with no picture should measure.
     */
    off_table += FNT_OFF_SLACK;

    for (i = 0; i <= (int)chars; i++)
        off_table[i] = word_at(file, (long)off_at + i * 2, motorola);

    for (i = 1; i <= FNT_OFF_SLACK; i++)
    {
        off_table[-i] = off_table[0];
        off_table[chars + i] = off_table[chars];
    }

    if (hor_table)
        for (i = 0; i < (int)chars * 2; i++)
            hor_table[i] = file[hor_at + i];

    /*
     * The raster, a word at a time so that an Intel one comes out the way a
     * 68000 would have written it. Every routine that draws a character reads
     * this in words and takes the high bit of one for the leftmost pixel, so a
     * font left in the other order draws each pair of columns swapped - which
     * is legible enough to be mistaken for a bad font rather than a bug.
     */
    for (i = 0; i < (int)raster_bytes; i += 2)
    {
        dat_table[i] = motorola ? file[dat_at + i] : file[dat_at + i + 1];
        dat_table[i + 1] = motorola ? file[dat_at + i + 1] : file[dat_at + i];
    }

    font->flags |= F_STDFORM;

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

    /* The pointer handed out points into the middle of what was allocated -
     * see the padding in gdos_font_read */
    host_vdi_free((void *)(font->off_table - FNT_OFF_SLACK));
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


/* ASSIGN.SYS *************************************************************/

/*
 * The list of fonts a machine is to have, and where it is.
 *
 * ASSIGN.SYS is a plain text file, one device to a paragraph. A line beginning
 * with a digit opens one - the device number, an optional letter or two of
 * flags, and the name of a driver - and the lines under it name that device's
 * fonts. Everything else is a remark. The device numbers are the same ones the
 * AES opens the physical workstation with, the screen's being its resolution
 * plus two, which is how a section is picked without asking anybody: there is
 * one output device here and it is the screen.
 *
 * The PATH line at the top says where the fonts are. In the files that came
 * with the applications it usually names a floppy - A:\GDOS.SYS - and tosemu
 * has no floppy, so a path that leads nowhere falls back to the directory
 * ASSIGN.SYS itself is in. That is not fidelity, it is the only reading under
 * which a program's own font directory can be copied off a disk and used where
 * it lands, which is what everybody has.
 */

/* Long enough for the longest ASSIGN.SYS anybody wrote, and bounded so that a
 * file that is not one cannot ask for memory until there is none */
#define GDOS_MAX_FONTS (256)

static struct {
    int looked;                     /* whether the search has been done */
    int loaded;                     /* whether the fonts have been read in */

    char where[PATH_MAX + 1];       /* the directory the fonts are in */
    char *name[GDOS_MAX_FONTS];     /* and what they are called */
    int names;

    Fonthead *font[GDOS_MAX_FONTS];
    int fonts;
    Fonthead *chain;

    WORD *scratch;
    WORD half;
} assign;

/* Whether a host path names something that can be opened for reading */
static int readable(const char *host_path)
{
    FILE *f = fopen(host_path, "rb");

    if (!f)
        return 0;

    fclose(f);

    return 1;
}

/* And whether one names a directory, which is what a PATH line names */
static int is_directory(const char *host_path)
{
    struct stat about;

    return stat(host_path, &about) == 0 && S_ISDIR(about.st_mode);
}

/*
 * The directory part of a path, without the separator. Written out because
 * dirname wants a string it may edit and answers with storage of its own.
 */
static void directory_of(const char *host_path, char *into)
{
    const char *slash = 0, *at;

    for (at = host_path; *at; at++)
        if (*at == '/')
            slash = at;

    if (!slash)
    {
        into[0] = '.';
        into[1] = 0;
        return;
    }

    snprintf(into, PATH_MAX + 1, "%.*s",
             (int)(slash == host_path ? 1 : slash - host_path), host_path);
}

/*
 * Where ASSIGN.SYS is, in the order the places are tried.
 *
 * What the settings say comes first, because saying it is saying it about this
 * run. Then the two places a program of the period would have looked for a
 * file of its own: beside the program, and where the process is standing -
 * which are usually the same directory and are not always, tosemu not moving
 * to the program's own. Last the root of the drive, which is where a GDOS.PRG
 * in the AUTO folder read it from.
 */
static int find_assign(char *into)
{
    const char *said = setting("TOSEMU_FONTS_ASSIGN");
    char tos[PATH_MAX + 1];
    int i;

    if (said)
    {
        /* Whatever was said, through the same path translation every other
         * file goes through, so that C:\GDOS\ASSIGN.SYS and an ordinary host
         * path both mean what they look like */
        if (tos_path_to_host(said, into) < 0 || !readable(into))
        {
            fprintf(stderr, "tosemu: there is no ASSIGN.SYS at %s\n", said);
            return 0;
        }

        return 1;
    }

    for (i = 0; i < 2; i++)
    {
        snprintf(into, PATH_MAX + 1, "%s/ASSIGN.SYS",
                 i == 0 ? tos_program_dir() : ".");

        if (readable(into))
            return 1;
    }

    snprintf(tos, sizeof tos, "C:\\ASSIGN.SYS");
    if (tos_path_to_host(tos, into) == 0 && readable(into))
        return 1;

    return 0;
}

/* Everything up to the newline, with the carriage return an Atari text file
 * ends its lines with taken off as well */
static void trim(char *line)
{
    char *at;

    for (at = line; *at; at++)
        if (*at == '\n' || *at == '\r')
        {
            *at = 0;
            return;
        }
}

static char *skip_blanks(char *at)
{
    while (*at == ' ' || *at == '\t')
        at++;

    return at;
}

static void trim_trailing(char *line)
{
    size_t len = strlen(line);

    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t'))
        line[--len] = 0;
}

/*
 * A PATH line, which is where the fonts are.
 *
 * It is written as a TOS path and it usually names a drive that is not here -
 * A:\GDOS.SYS is what every ASSIGN.SYS that came with an application says,
 * the fonts having been on the floppy the application came on. So a path that
 * does not lead to a directory is the ordinary case rather than a mistake, and
 * what is done about it decides whether a program's font directory can be
 * copied off its disk and used where it lands.
 *
 * What is kept from such a path is its tail. A:\GDOS.SYS names a directory
 * called GDOS.SYS, and the copy of it is beside ASSIGN.SYS under that name,
 * because whoever copied the disk copied the whole of it. So the drive is
 * dropped, the separators turned round, and what is left looked for where the
 * file that named it is. Failing that, the directory ASSIGN.SYS is in, which
 * is where a flattened copy would have put them.
 */
static void take_path(const char *said, const char *assign_at)
{
    char host[PATH_MAX + 1];
    char here[PATH_MAX + 1];
    char tail[PATH_MAX + 1];
    const char *from = said;
    int i;

    if (tos_path_to_host(said, host) == 0 && is_directory(host))
    {
        snprintf(assign.where, sizeof assign.where, "%s", host);
        return;
    }

    directory_of(assign_at, here);

    /* The drive letter, and then any separators after it */
    if (from[0] && from[1] == ':')
        from += 2;
    while (*from == '\\' || *from == '/')
        from++;

    for (i = 0; from[i] && i < PATH_MAX; i++)
        tail[i] = (from[i] == '\\') ? '/' : from[i];
    tail[i] = 0;

    if (tail[0])
    {
        snprintf(host, sizeof host, "%s/%s", here, tail);

        if (is_directory(host))
        {
            snprintf(assign.where, sizeof assign.where, "%s", host);
            return;
        }
    }

    snprintf(assign.where, sizeof assign.where, "%s", here);
}

/*
 * Whether a line opens a device section, and which device.
 *
 * "01p SCREEN.SYS" and "21 FX80.SYS" are both of them: a number, optionally a
 * letter saying the fonts are to stay in memory, then the driver. The driver
 * name is not used - the screen driver is the VDI itself, and there is no
 * other device here - so only the number is read.
 */
static int device_line(const char *line, WORD *device)
{
    int value = 0, digits = 0;

    if (*line < '0' || *line > '9')
        return 0;

    while (*line >= '0' && *line <= '9')
    {
        value = value * 10 + (*line++ - '0');
        digits++;
    }

    if (digits > 3)
        return 0;

    *device = (WORD)value;

    return 1;
}

/*
 * A copy of a string on the heap. EmuTOS has a string.h of its own and it
 * comes first on the include path, which is what lets the rest of the port
 * compile - and it declares the handful of string functions EmuTOS uses, of
 * which this is not one.
 */
static char *copy_of(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);

    if (copy)
        memcpy(copy, s, len);

    return copy;
}

static void forget_names(void)
{
    int i;

    for (i = 0; i < assign.names; i++)
        free(assign.name[i]);

    assign.names = 0;
}

void gdos_assign_init(WORD device)
{
    char path[PATH_MAX + 1];
    char line[512];
    FILE *f;
    WORD in_device = 0;
    int ours = 0;

    if (assign.looked)
        return;

    assign.looked = 1;

    if (!find_assign(path))
        return;

    f = fopen(path, "r");
    if (!f)
        return;

    /* Where the fonts are, until a PATH line says otherwise */
    directory_of(path, assign.where);

    while (fgets(line, sizeof line, f))
    {
        char *at;
        WORD named;

        trim(line);
        at = skip_blanks(line);
        trim_trailing(at);

        if (!*at || *at == ';' || *at == '#')
            continue;

        if (strncasecmp(at, "PATH", 4) == 0 && strchr(at, '='))
        {
            take_path(skip_blanks(strchr(at, '=') + 1), path);
            continue;
        }

        if (device_line(at, &named))
        {
            in_device = named;
            ours = (named == device);
            continue;
        }

        if (!ours || in_device == 0)
            continue;

        if (assign.names >= GDOS_MAX_FONTS)
        {
            fprintf(stderr, "tosemu: %s names more than %d fonts for device "
                    "%d, and the rest are ignored\n",
                    path, GDOS_MAX_FONTS, device);
            break;
        }

        assign.name[assign.names] = copy_of(at);
        if (!assign.name[assign.names])
            break;

        assign.names++;
    }

    fclose(f);

    /* TOSEMU_TRACE_PATHS says which files were looked for and what they came
     * to, and which ASSIGN.SYS was found is the first thing to want when an
     * application reports no fonts */
    if (setting_flag("TOSEMU_TRACE_PATHS"))
        fprintf(stderr, "tosemu: %s names %d fonts for device %d, in %s\n",
                path, assign.names, device, assign.where);
}

int gdos_installed(void)
{
    return assign.names > 0;
}

Fonthead *gdos_loaded_chain(void)
{
    int i;
    WORD half;

    if (assign.loaded)
        return assign.chain;

    assign.loaded = 1;

    /*
     * The buffer has to hold the largest character of any font that will be
     * drawn through it, and once it is handed over it is used for the system
     * fonts as well - so it starts at the size the VDI worked out for those
     * and grows to fit whatever is loaded.
     */
    half = SCRATCHBUF_SIZE / 2;

    for (i = 0; i < assign.names; i++)
    {
        char path[PATH_MAX + 1];
        Fonthead *font;

        snprintf(path, sizeof path, "%s/%s", assign.where, assign.name[i]);

        font = gdos_font_read(path);
        if (!font)
            continue;

        if (gdos_font_scratch(font) > half)
            half = gdos_font_scratch(font);

        assign.font[assign.fonts++] = font;
    }

    forget_names();

    assign.chain = gdos_font_chain(assign.font, assign.fonts);

    /* Two halves, which is what scrpt2 is the offset to. vdi_text.c wants both
     * of them large enough for a doubled, rotated, outlined character. */
    assign.scratch = host_vdi_alloc((long)half * 2);
    assign.half = assign.scratch ? half : 0;

    if (!assign.scratch)
    {
        fprintf(stderr, "tosemu: there was no room for the buffer the loaded "
                "fonts are drawn through, so there are none\n");
        assign.chain = 0;
    }

    return assign.chain;
}

WORD *gdos_loaded_scratch(WORD *half)
{
    *half = assign.half;

    return assign.scratch;
}

/*
 * What vst_load_fonts comes to, and the reason it is here rather than left to
 * EmuTOS is in gdos.h: the call it has cannot be reached from a host where a
 * ULONG is eight bytes.
 *
 * What it does is the bookkeeping the VDI needs to see loaded fonts at all.
 * font_ring is where every call that walks the fonts starts - vqt_name,
 * vst_font, vst_point, vst_height - and its third entry is the one kept for
 * these. num_fonts is how many faces the workstation reports. The buffer is
 * the one characters are built in before they reach the screen, and it has to
 * be the larger one now that there are larger characters to build.
 */
/*
 * How many faces there are, counted the way vqt_name counts them.
 *
 * It walks the whole ring rather than one chain, and a face is a place where
 * the id changes as it goes - so a font whose id is one the ring already has
 * is another size of that face rather than a face of its own. That is what
 * GDOS was for as much as anything: a .FNT carrying face id 1 adds a size to
 * the system font.
 *
 * Counting it any other way is how the number an application is given stops
 * agreeing with the list it can then ask for, and vst_load_fonts is the one
 * number applications trust.
 */
static WORD ring_faces(void)
{
    const Fonthead *font, * const *chain = font_ring;
    WORD id = -1, count = 0;

    while ((font = *chain++))
    {
        do {
            if (font->font_id != id)
            {
                id = font->font_id;
                count++;
            }
        } while ((font = font->next_font));
    }

    return count;
}

WORD gdos_install(Vwk *vwk)
{
    Fonthead *chain;
    WORD *scratch, half = 0;
    WORD before, count;

    /* One chance, which is EmuTOS's rule and TOS's before it. Asking twice
     * answers with no faces that were not already there rather than counting
     * the same ones again. */
    if (vwk->loaded_fonts)
        return 0;

    chain = gdos_loaded_chain();
    scratch = gdos_loaded_scratch(&half);

    if (!chain || !scratch)
        return 0;

    before = ring_faces();

    vwk->scrtchp = scratch;
    vwk->scrpt2 = half;
    vwk->loaded_fonts = chain;
    font_ring[2] = chain;

    count = (WORD)(ring_faces() - before);
    vwk->num_fonts += count;

    return count;
}
