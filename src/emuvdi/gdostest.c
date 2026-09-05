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
 * the same font in every last byte, the raster included, because what the VDI
 * is handed has to be in the VDI's order whatever order it was written in.
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
#include <errno.h>

/* EmuTOS's own, which comes first on the include path and declares the handful
 * of string functions EmuTOS uses. strcmp is one of them. */
#include "string.h"

#include "gdos.h"
#include "emuvdi.h"

/* tosemu's own, written in plain types so that it can be reached from this
 * side - the outline engine cannot be, FreeType's headers and EmuTOS's not
 * being able to share a translation unit */
#include "../fontface.h"

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
/* Where the fonts go, under the directory ASSIGN.SYS is written into. The name
 * is the one every Atari installation used. */
#define SAMPLE_SUBDIR "GDOS.SYS"

#define SAMPLE_FIRST_ADE (32)
#define SAMPLE_LAST_ADE  (34)
#define SAMPLE_CHARS     (SAMPLE_LAST_ADE - SAMPLE_FIRST_ADE + 1)

#define SAMPLE_OFF_AT    (88)
#define SAMPLE_DAT_AT    (SAMPLE_OFF_AT + (SAMPLE_CHARS + 1) * 2)

#define SAMPLE_FORM_W    (2)
#define SAMPLE_FORM_H    (6)
#define SAMPLE_SIZE      (SAMPLE_DAT_AT + SAMPLE_FORM_W * SAMPLE_FORM_H)

/*
 * Where each character starts in the raster, which is what says how wide it
 * is. The two sizes of the one face are given different widths on purpose: a
 * test in which every size measured the same could not tell vst_point picking
 * the right font from vst_point picking any font at all.
 */
static const UWORD sample_offsets[SAMPLE_CHARS + 1] = { 0, 3, 8, 12 };
static const UWORD sample_offsets_large[SAMPLE_CHARS + 1] = { 0, 4, 10, 15 };

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
    const UWORD *offsets;   /* where each character starts in the raster */
};

/*
 * Three fonts, deliberately not in the order a chain wants them: two sizes of
 * one face with the larger first, and a second face in between. What comes out
 * of gdos_font_chain has to be the other order.
 */
static const struct sample samples[] = {
    { "TEST12.FNT", 100, 12, "Test Sans", 9, 7, 0, sample_offsets_large },
    { "MONO10.FNT", 101, 10, "Test Mono", 8, 4, F_MONOSPACE, sample_offsets },
    { "TEST08.FNT", 100,  8, "Test Sans", 6, 5, 0, sample_offsets }
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
        put_word(file, SAMPLE_OFF_AT + i * 2, s->offsets[i], motorola);

    for (i = 0; i < SAMPLE_FORM_H; i++)
        put_word(file, SAMPLE_DAT_AT + i * 2, sample_raster[i], motorola);
}

/*
 * An ASSIGN.SYS naming them, for the test that runs inside the emulator.
 *
 * Two device sections rather than one, because which section applies is a
 * decision tosemu makes rather than one an application makes: the number is
 * the screen's resolution plus two, so ST low is 2 and ST high is 4, and a
 * test that ran on one screen would not notice the choice being made at all.
 * ST low gets one face here and ST high gets two.
 *
 * The PATH line names a drive that is not there, which is not an oversight -
 * it is what every ASSIGN.SYS that came with an application says, the fonts
 * having been on a floppy. What has to survive that is the tail of it: the
 * fonts go in a directory called GDOS.SYS beside this file, exactly as they do
 * in an installation copied off its disk, and finding them there is the only
 * reading under which such a copy works at all.
 */
static int write_assign(const char *dir)
{
    char path[1024];
    FILE *f;

    snprintf(path, sizeof path, "%s/ASSIGN.SYS", dir);

    f = fopen(path, "w");
    if (!f)
        return 0;

    fprintf(f, "; written by gdostest, for the fonts beside it\r\n");
    fprintf(f, "PATH = A:\\%s\r\n", SAMPLE_SUBDIR);
    fprintf(f, "2p SCREEN.SYS\r\n");
    fprintf(f, "  %s\r\n", samples[1].file);
    fprintf(f, "4p SCREEN.SYS\r\n");
    fprintf(f, "  %s\r\n", samples[2].file);
    fprintf(f, "  %s\r\n", samples[0].file);
    fprintf(f, "  %s\r\n", samples[1].file);

    fclose(f);

    return 1;
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

    /* Set, whatever the file said, because what comes out of here is in the
     * VDI's order and the flag is how the VDI is told so */
    check((intel->flags & F_STDFORM) != 0, 1,
          "and arrives the way round the VDI reads a raster");

    /* The same font the other way round. It has to come out identical - the
     * header, the widths and the raster - because the byte order is the only
     * thing that differed and it is the thing being undone. */
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

    ir = (const UBYTE *)intel->dat_table;
    mr = (const UBYTE *)motorola->dat_table;

    for (i = 0; i < SAMPLE_FORM_W * SAMPLE_FORM_H; i++)
        if (ir[i] != mr[i])
            swapped = 0;

    check(swapped, 1, "and a raster identical to the Intel one");

    /* And it is the raster that was written rather than either half of it
     * left as it lay: the first row went in as 0x1234 and has to read back
     * that way round, high byte first, which is where a 68000 keeps the
     * leftmost eight pixels */
    check((mr[0] << 8) | mr[1], 0x1234,
          "with the leftmost pixels of its first row in the high byte");

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

/* The faces that are not files ********************************************/

/*
 * The outline engine, which is the other half of the fonts: a face asked for
 * by the name a document was written in, at whatever size, rather than at the
 * sizes somebody put .FNT files on the disk for.
 *
 * What can be checked here is narrow on purpose. Which face a name resolves to
 * depends on what fonts the machine has, and how wide a word comes out depends
 * on the face - so an expected number would be a number about this computer.
 * What does hold everywhere is that every name resolved to something, that the
 * faces are distinct, that a larger size measures larger, and that drawing
 * puts ink somewhere. A build without FreeType has no faces and says so.
 */
static void check_the_outline_faces(void)
{
    struct fontface_bitmap bitmap;
    const char *text = "Hamburgefonstiv";
    int faces = fontface_init();
    int i, small = 0, large = 0, ink = 0;

    if (faces == 0)
    {
        printf("# no outline faces, so this is a build without FreeType or a "
               "machine with no fonts it could use\n");
        return;
    }

    check(faces > 0, 1, "the outline engine offers faces");

    for (i = 0; i < faces; i++)
    {
        printf("# %-20s set in %s\n", fontface_name(i), fontface_resolved(i));

        if (fontface_resolved(i)[0] == 0)
            break;
    }

    check(i, faces, "every name in the table resolved to a face on this host");

    for (i = 0; i < faces; i++)
        if (!fontface_known(fontface_id(i)) || fontface_id(i) == 0)
            break;

    check(i, faces, "and each has a number the VDI can be asked for");

    /*
     * Twelve point and twenty four point of the first face. The larger has to
     * measure larger - which is the whole of what a scalable font is - and it
     * is checked as a comparison rather than against a number because the
     * number is a fact about whichever font this machine substituted.
     */
    fontface_extent(fontface_id(0), 12 * 64, text, (int)strlen(text),
                    &small, 0);
    fontface_extent(fontface_id(0), 24 * 64, text, (int)strlen(text),
                    &large, 0);

    check(small > 0, 1, "a string has a width at twelve point");
    check(large > small, 1, "and a larger one at twenty four");

    /* And that drawing it puts something down. A renderer that quietly drew
     * nothing would pass every measurement above. */
    if (fontface_render(fontface_id(0), 24 * 64, text, (int)strlen(text),
                        &bitmap))
    {
        int word;

        for (word = 0; word < bitmap.words * bitmap.height; word++)
            if (bitmap.bits[word])
                ink++;

        check(bitmap.width >= large, 1,
              "the bitmap it is drawn into is at least as wide as the string");
        check(ink > 0, 1, "and there is ink in it");

        fontface_render_free(&bitmap);
    }
    else
        check(0, 1, "a string can be drawn");
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
        snprintf(path, sizeof path, "%s/%s", argv[2], SAMPLE_SUBDIR);

        if (mkdir(path, 0777) != 0 && errno != EEXIST)
        {
            fprintf(stderr, "gdostest: %s could not be made\n", path);
            return 1;
        }

        for (i = 0; i < SAMPLE_COUNT; i++)
            if (!write_sample(path, &samples[i], 0))
            {
                fprintf(stderr, "gdostest: %s/%s could not be written\n",
                        path, samples[i].file);
                return 1;
            }

        if (!write_assign(argv[2]))
        {
            fprintf(stderr, "gdostest: %s/ASSIGN.SYS could not be written\n",
                    argv[2]);
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
    check_the_outline_faces();

    for (i = 0; i < SAMPLE_COUNT; i++)
    {
        snprintf(path, sizeof path, "%s/%s", dir, samples[i].file);
        unlink(path);
    }
    rmdir(dir);

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
