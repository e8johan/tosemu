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
 * Does a .FNT file come in the way the VDI expects to find one?
 *
 * Built for the host rather than for the emulated machine, because what is
 * being checked is underneath the emulator: byte order, where the tables are,
 * and the order a chain of fonts is put in. None of that is visible from
 * inside a program - what an application sees is a name in a menu and a word
 * in the wrong place - and all of it is easy to get wrong in a way that still
 * draws something.
 *
 * The fonts are written here rather than carried, and that is deliberate. A
 * real .FNT is tens of kilobytes of raster nobody can check by eye, and the
 * ones Atari shipped are somebody else's to redistribute. These are three
 * characters wide, their widths and their raster are numbers chosen in this
 * file, and the answers below are worked out from them by hand.
 *
 * The same font is written twice, once in each byte order, which is the case
 * that matters: every .FNT Atari shipped is Intel, having been made on a PC,
 * and the VDI that reads it was written for a 68000. The two must come in as
 * the same font in everything but the raster, which is left as the file had it
 * for the VDI to turn over with the routine it already has.
 *
 * With --write it puts the same fonts into a directory instead of checking
 * them, which is how the test that runs inside the emulator gets fonts to
 * load. With --list it prints the header of every font named after it, which
 * is for looking at a real GDOS.SYS.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* EmuTOS's own, which comes first on the include path and declares the handful
 * of string functions EmuTOS uses. strcmp is one of them. */
#include "string.h"

#include "gdos.h"
#include "emuvdi.h"

/* The 8x16 system font, which is the one both sides know about */
extern const Fonthead fnt_st_8x16;

static int n;
static int fails;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
    }
}

/* Building a font file *****************************************************/

/*
 * What the sample fonts are. Three characters - a space and two others - so
 * that the offset table has something in it to get wrong, and a raster of six
 * rows of one word, whose bytes are all different so that a word turned over
 * is a different number rather than the same one.
 */
#define SAMPLE_FIRST_ADE (32)
#define SAMPLE_LAST_ADE  (34)
#define SAMPLE_CHARS     (SAMPLE_LAST_ADE - SAMPLE_FIRST_ADE + 1)

#define SAMPLE_OFF_AT    (88)
#define SAMPLE_DAT_AT    (SAMPLE_OFF_AT + (SAMPLE_CHARS + 1) * 2)

#define SAMPLE_FORM_W    (2)
#define SAMPLE_FORM_H    (6)
#define SAMPLE_SIZE      (SAMPLE_DAT_AT + SAMPLE_FORM_W * SAMPLE_FORM_H)

/* Where each character starts in the raster, so the widths are 3, 5 and 4 */
static const UWORD sample_offsets[SAMPLE_CHARS + 1] = { 0, 3, 8, 12 };

/* One word per row, no two bytes alike */
static const UWORD sample_raster[SAMPLE_FORM_H] = {
    0x1234, 0x5678, 0x9abc, 0xdef0, 0x0f1e, 0x2d3c
};

struct sample {
    const char *file;       /* what to call it */
    WORD font_id;
    WORD point;
    const char *name;
    WORD top;
    WORD max_cell_width;
    WORD extra_flags;       /* F_MONOSPACE and the like */
};

/*
 * Three fonts, deliberately not in the order a chain wants them: two sizes of
 * one face with the larger first, and a second face in between. What comes out
 * of gdos_font_chain has to be the other order.
 */
static const struct sample samples[] = {
    { "TEST12.FNT", 100, 12, "Test Sans", 9, 5, 0 },
    { "MONO10.FNT", 101, 10, "Test Mono", 8, 4, F_MONOSPACE },
    { "TEST08.FNT", 100,  8, "Test Sans", 6, 5, 0 }
};

#define SAMPLE_COUNT ((int)(sizeof samples / sizeof samples[0]))

static void put_word(UBYTE *file, long at, UWORD value, int motorola)
{
    if (motorola)
    {
        file[at] = (UBYTE)(value >> 8);
        file[at + 1] = (UBYTE)value;
    }
    else
    {
        file[at] = (UBYTE)value;
        file[at + 1] = (UBYTE)(value >> 8);
    }
}

static void put_long(UBYTE *file, long at, ULONG value, int motorola)
{
    if (motorola)
    {
        put_word(file, at, (UWORD)(value >> 16), 1);
        put_word(file, at + 2, (UWORD)value, 1);
    }
    else
    {
        put_word(file, at, (UWORD)value, 0);
        put_word(file, at + 2, (UWORD)(value >> 16), 0);
    }
}

/*
 * A whole font file, in the byte order asked for. F_STDFORM is what says which
 * order it is in, so it is set for the Motorola one and clear for the Intel
 * one, which is exactly what the files Atari shipped do.
 */
static void build(UBYTE *file, const struct sample *s, int motorola)
{
    int i;

    for (i = 0; i < SAMPLE_SIZE; i++)
        file[i] = 0;

    put_word(file, 0, (UWORD)s->font_id, motorola);
    put_word(file, 2, (UWORD)s->point, motorola);

    for (i = 0; s->name[i]; i++)
        file[4 + i] = (UBYTE)s->name[i];

    put_word(file, 36, SAMPLE_FIRST_ADE, motorola);
    put_word(file, 38, SAMPLE_LAST_ADE, motorola);
    put_word(file, 40, (UWORD)s->top, motorola);            /* top */
    put_word(file, 42, (UWORD)(s->top - 1), motorola);      /* ascent */
    put_word(file, 44, (UWORD)(s->top / 2), motorola);      /* half */
    put_word(file, 46, 1, motorola);                        /* descent */
    put_word(file, 48, 2, motorola);                        /* bottom */
    put_word(file, 50, (UWORD)(s->max_cell_width - 1), motorola);
    put_word(file, 52, (UWORD)s->max_cell_width, motorola);
    put_word(file, 54, 1, motorola);                        /* left_offset */
    put_word(file, 56, 2, motorola);                        /* right_offset */
    put_word(file, 58, 1, motorola);                        /* thicken */
    put_word(file, 60, 1, motorola);                        /* ul_size */
    put_word(file, 62, 0x5555, motorola);                   /* lighten */
    put_word(file, 64, 0x5555, motorola);                   /* skew */
    put_word(file, 66, (UWORD)(s->extra_flags | (motorola ? F_STDFORM : 0)),
             motorola);

    put_long(file, 68, SAMPLE_OFF_AT, motorola);            /* hor_table */
    put_long(file, 72, SAMPLE_OFF_AT, motorola);            /* off_table */
    put_long(file, 76, SAMPLE_DAT_AT, motorola);            /* dat_table */

    put_word(file, 80, SAMPLE_FORM_W, motorola);
    put_word(file, 82, SAMPLE_FORM_H, motorola);

    put_long(file, 84, 0, motorola);                        /* next_font */

    for (i = 0; i <= SAMPLE_CHARS; i++)
        put_word(file, SAMPLE_OFF_AT + i * 2, sample_offsets[i], motorola);

    for (i = 0; i < SAMPLE_FORM_H; i++)
        put_word(file, SAMPLE_DAT_AT + i * 2, sample_raster[i], motorola);
}

static int write_file(const char *path, const UBYTE *data, long size)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (!f)
        return 0;

    ok = fwrite(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);

    return ok;
}

/* One of the samples, written where it is asked for */
static int write_sample(const char *dir, const struct sample *s, int motorola)
{
    UBYTE file[SAMPLE_SIZE];
    char path[1024];

    build(file, s, motorola);
    snprintf(path, sizeof path, "%s/%s", dir, s->file);

    return write_file(path, file, SAMPLE_SIZE);
}

/* The checks ***************************************************************/

static void check_one_font(const char *dir)
{
    char path[1024];
    Fonthead *intel, *motorola;
    const UBYTE *ir, *mr;
    int i, same = 1, swapped = 1;

    snprintf(path, sizeof path, "%s/%s", dir, samples[2].file);
    write_sample(dir, &samples[2], 0);
    intel = gdos_font_read(path);

    check(intel != 0, 1, "a font in Intel order is read");
    if (!intel)
        return;

    check(intel->font_id, 100, "with the face it says it is");
    check(intel->point, 8, "at the size it says it is");
    check(strcmp(intel->name, "Test Sans"), 0, "under its own name");
    check(intel->first_ade, SAMPLE_FIRST_ADE, "starting at the character it says");
    check(intel->last_ade, SAMPLE_LAST_ADE, "and ending at the one it says");
    check(intel->form_width, SAMPLE_FORM_W, "its raster as wide as it says");
    check(intel->form_height, SAMPLE_FORM_H, "and as tall");

    /* The offset table is what every width the VDI answers with comes out of,
     * so a byte order mistake here is a document laid out wrongly rather than
     * a font that fails to load */
    check(intel->off_table[1] - intel->off_table[0], 3,
          "the first character is as wide as the offset table says");
    check(intel->off_table[2] - intel->off_table[1], 5, "and the second");
    check(intel->off_table[3] - intel->off_table[2], 4, "and the third");

    /* Left clear, because vdi_vst_load_fonts is the one that turns the raster
     * over and it decides by this flag */
    check(intel->flags & F_STDFORM, 0,
          "an Intel font arrives with its raster still to be turned over");

    /* The same font the other way round. Everything the header says has to
     * come out the same; the raster has to come out byte swapped, because that
     * is the difference the flag stands for. */
    write_sample(dir, &samples[2], 1);
    motorola = gdos_font_read(path);

    check(motorola != 0, 1, "and the same font in Motorola order");
    if (!motorola)
        return;

    check(motorola->font_id == intel->font_id
          && motorola->point == intel->point
          && motorola->top == intel->top
          && motorola->max_cell_width == intel->max_cell_width
          && motorola->first_ade == intel->first_ade
          && motorola->last_ade == intel->last_ade
          && motorola->form_width == intel->form_width
          && motorola->form_height == intel->form_height, 1,
          "which says everything the Intel one said");

    for (i = 0; i <= SAMPLE_CHARS; i++)
        if (motorola->off_table[i] != intel->off_table[i])
            same = 0;

    check(same, 1, "and has the same widths in it");

    check((motorola->flags & F_STDFORM) != 0, 1,
          "a Motorola font arrives with its raster already the right way round");

    ir = (const UBYTE *)intel->dat_table;
    mr = (const UBYTE *)motorola->dat_table;

    for (i = 0; i < SAMPLE_FORM_W * SAMPLE_FORM_H; i += 2)
        if (ir[i] != mr[i + 1] || ir[i + 1] != mr[i])
            swapped = 0;

    check(swapped, 1,
          "and a raster that is the Intel one word for word the other way round");

    gdos_font_free(intel);
    gdos_font_free(motorola);
}

static void check_the_chain(const char *dir)
{
    Fonthead *font[SAMPLE_COUNT];
    Fonthead *head;
    char path[1024];
    int i, read = 0;

    for (i = 0; i < SAMPLE_COUNT; i++)
    {
        write_sample(dir, &samples[i], 0);
        snprintf(path, sizeof path, "%s/%s", dir, samples[i].file);
        font[i] = gdos_font_read(path);
        if (font[i])
            read++;
    }

    check(read, SAMPLE_COUNT, "three fonts are read");
    if (read != SAMPLE_COUNT)
        return;

    head = gdos_font_chain(font, SAMPLE_COUNT);

    /*
     * The order is what vqt_name counts faces by and what vst_point picks a
     * size by, so it is the whole of what a chain is for. They were read
     * largest first with the other face in between them.
     */
    check(head != 0, 1, "and chained together");
    check(head->font_id, 100, "the first is the lower numbered face");
    check(head->point, 8, "at its smallest size");
    check(head->next_font->font_id, 100, "the second is the same face");
    check(head->next_font->point, 12, "at the size above it");
    check(head->next_font->next_font->font_id, 101,
          "and the other face comes after both of them");
    check(head->next_font->next_font->next_font == 0, 1,
          "with nothing after it");

    for (i = 0; i < SAMPLE_COUNT; i++)
        gdos_font_free(font[i]);
}

static void check_refusals(const char *dir)
{
    UBYTE file[SAMPLE_SIZE];
    char path[1024];

    snprintf(path, sizeof path, "%s/BAD.FNT", dir);

    printf("# four files that are not fonts, each of which says so\n");

    /* Shorter than a header */
    build(file, &samples[2], 0);
    write_file(path, file, 40);
    check(gdos_font_read(path) == 0, 1, "a file too short to be a header is refused");

    /* A raster that is not a whole number of words across, which nothing that
     * draws a character could work with */
    build(file, &samples[2], 0);
    put_word(file, 80, 3, 0);
    write_file(path, file, SAMPLE_SIZE);
    check(gdos_font_read(path) == 0, 1,
          "a raster an odd number of bytes wide is refused");

    /* Characters in an order there is no font for */
    build(file, &samples[2], 0);
    put_word(file, 36, 200, 0);
    write_file(path, file, SAMPLE_SIZE);
    check(gdos_font_read(path) == 0, 1,
          "a font whose first character is after its last is refused");

    /* A table that reaches past the end of the file, which is the shape a
     * truncated download arrives in */
    build(file, &samples[2], 0);
    write_file(path, file, SAMPLE_DAT_AT + 2);
    check(gdos_font_read(path) == 0, 1,
          "a font whose raster runs off the end of the file is refused");

    unlink(path);
}

/*
 * The scratch buffer, checked against the one font both sides know about.
 *
 * vdi_text.c works out how large a buffer the 8x16 system font needs at
 * compile time and calls the answer SCRATCHBUF_SIZE. gdos_font_scratch does
 * the same arithmetic at run time for a font that was not there to be measured
 * when the VDI was built, and if the two disagree about the font they can both
 * see, they will disagree about every other font as well.
 */
static void check_the_scratch_buffer(void)
{
    check(gdos_font_scratch(&fnt_st_8x16), SCRATCHBUF_SIZE / 2,
          "a font needs the buffer the VDI worked out for the system font");
}

/* Looking at somebody else's fonts *****************************************/

static void list(int count, char **path)
{
    int i;

    for (i = 0; i < count; i++)
    {
        Fonthead *font = gdos_font_read(path[i]);

        if (!font)
            continue;

        printf("%-16s id %3d  %-16s %3d point  %2d-%3d  cell %2dx%-2d  "
               "raster %4dx%-3d  %s\n",
               path[i], font->font_id, font->name, font->point,
               font->first_ade, font->last_ade,
               font->max_cell_width, font->top + font->bottom + 1,
               font->form_width, font->form_height,
               (font->flags & F_STDFORM) ? "Motorola" : "Intel");

        gdos_font_free(font);
    }
}

int main(int argc, char **argv)
{
    char dir[] = "/tmp/tosemu-gdos-XXXXXX";
    char path[1024];
    int i;

    if (argc > 2 && strcmp(argv[1], "--write") == 0)
    {
        for (i = 0; i < SAMPLE_COUNT; i++)
            if (!write_sample(argv[2], &samples[i], 0))
            {
                fprintf(stderr, "gdostest: %s/%s could not be written\n",
                        argv[2], samples[i].file);
                return 1;
            }

        return 0;
    }

    if (argc > 2 && strcmp(argv[1], "--list") == 0)
    {
        list(argc - 2, argv + 2);
        return 0;
    }

    /* Somewhere of its own to write the samples, since a check that leaves
     * files behind in whichever directory it was run from is a check nobody
     * wants to run twice */
    if (!mkdtemp(dir))
    {
        printf("Bail out! - nowhere to write the sample fonts\n");
        return 1;
    }

    check_one_font(dir);
    check_the_chain(dir);
    check_refusals(dir);
    check_the_scratch_buffer();

    for (i = 0; i < SAMPLE_COUNT; i++)
    {
        snprintf(path, sizeof path, "%s/%s", dir, samples[i].file);
        unlink(path);
    }
    rmdir(dir);

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
