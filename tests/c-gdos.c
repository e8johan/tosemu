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

/* Asking about GDOS, and asking it for fonts.
 *
 * vq_gdos is a knock on the door: the answer says whether anything above the
 * system fonts is there, and an application takes one road or the other from
 * it. vst_load_fonts is the first thing it calls if the answer was yes.
 *
 * That second call is why this file started. EmuTOS implements it for the
 * interface real GDOS filled in rather than the one an application calls
 * through - it reads the head of the font chain out of control[10-11] - and
 * those two words are whatever the caller's array happened to hold. So a
 * program that asked for fonts on a machine that has none was reading a
 * pointer out of its own leftovers, and the count line at the end of this file
 * is what says it stopped doing so: an emulator that walked that chain never
 * reaches the end of main.
 *
 * The rest of it is the fonts themselves. The samples are bin/gdostest's,
 * written into this directory before the run along with an ASSIGN.SYS naming
 * them, so that the widths and sizes checked here are numbers somebody chose
 * rather than numbers read out of a font nobody can open.
 *
 * Which of them arrive depends on the screen, and that is the point of running
 * this twice. A device section in ASSIGN.SYS is numbered by the resolution
 * plus two, so the ST high screen reads section 4 and the ST low screen reads
 * section 2 - and the sample file gives them different fonts. The argument
 * says which screen this run is on and therefore what it should find.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

static short work_in[11];
static short work_out[57];
static short handle;

/* What the sample fonts are, which is bin/gdostest's business and repeated
 * here because a test that read them out of the font would pass whatever the
 * font said */
#define SANS_ID (100)
#define MONO_ID (101)

/*
 * The widths in TEST08.FNT: its three characters are the space, ! and ", and
 * they are 3, 5 and 4 pixels wide. So "!\"" is 9 across, and that number comes
 * out of the offset table by way of vqt_extent - which is the whole of what a
 * document laid out in a loaded font depends on.
 */
#define SANS08_EXTENT (3 + 5 + 4)

/* And in TEST12.FNT, whose characters are wider - which is what makes the
 * check below a check that vst_point found the larger font rather than a
 * check that it found a font */
#define SANS12_EXTENT (4 + 6 + 5)

/*
 * The face at a place in the list. The VDI answers with the id in intout[0]
 * and the name one character to a word after it; the binding unpacks the name
 * into a string and hands back the id, which is the shape used here.
 */
static short name_of(short element, char *into)
{
    return vqt_name(handle, element, into);
}

/* And the id of the face with a given name, or 0. The list is walked rather
 * than indexed because what is in it besides the loaded fonts depends on how
 * the build was made. */
static short face_called(const char *wanted, short faces)
{
    char name[33];
    short i, id;

    for (i = 1; i <= faces; i++)
    {
        id = name_of(i, name);

        if (strcmp(name, wanted) == 0)
            return id;
    }

    return 0;
}

/*
 * How many pixels in a box are not the background, which is how a string that
 * was drawn is told from one that was not. v_get_pixel answers with the colour
 * index, and everything here is drawn in colour 1 on a screen that starts as
 * colour 0.
 */
/* And which colour that ink is, taking the first one found */
static short ink_colour(short x1, short x2, short y1, short y2)
{
    short x, y, pel, index;

    for (y = y1; y < y2; y++)
        for (x = x1; x < x2; x++)
        {
            v_get_pixel(handle, x, y, &pel, &index);
            if (index != 0)
                return index;
        }

    return 0;
}

static int ink_between(short x1, short x2, short y1, short y2)
{
    short x, y, pel, index;
    int ink = 0;

    for (y = y1; y < y2; y++)
        for (x = x1; x < x2; x++)
        {
            v_get_pixel(handle, x, y, &pel, &index);
            if (index != 0)
                ink++;
        }

    return ink;
}

/* How many of the sample fonts are in the list, which is what says the right
 * section of ASSIGN.SYS was read without having to know what else is there */
static short samples_among(short faces)
{
    char name[33];
    short i, found = 0;

    for (i = 1; i <= faces; i++)
    {
        name_of(i, name);

        if (strcmp(name, "Test Sans") == 0 || strcmp(name, "Test Mono") == 0)
            found++;
    }

    return found;
}

/*
 * The calls above 230, which have no bindings in the library: gemlib knows
 * vq_gdos and vst_load_fonts and stops well short of v_ftext. So the parameter
 * block is filled in by hand, which is all a binding is.
 */
static short p_control[VDI_CNTRLMAX];
static short p_intin[128];
static short p_ptsin[16];
static short p_intout[128];
static short p_ptsout[16];

static void speedo(short opcode, short points, short values)
{
    VDIPB pb;

    p_control[0] = opcode;
    p_control[1] = points;
    p_control[3] = values;
    p_control[5] = 0;
    p_control[6] = handle;

    pb.control = p_control;
    pb.intin = p_intin;
    pb.ptsin = p_ptsin;
    pb.intout = p_intout;
    pb.ptsout = p_ptsout;

    vdi(&pb);
}

/* A string into intin, one character to a word, which is how every VDI text
 * call takes one */
static short put_text(const char *text)
{
    short i;

    for (i = 0; text[i]; i++)
        p_intin[i] = (unsigned char)text[i];

    return i;
}

int main(int argc, char **argv)
{
    short i;
    long version;
    short extent[8];
    int faces_wanted;
    short faces_before, faces, loaded, sans, mono;
    int outlines;

    if (argc < 2)
    {
        printf("Bail out! - no screen named to expect\n");
        return 1;
    }

    /*
     * Section 4 of the sample ASSIGN.SYS has two faces in it and section 2 has
     * one. Which is read is decided by the screen and by nothing else.
     *
     * "none" is the run with no ASSIGN.SYS to be found anywhere, which is a
     * machine with no GDOS installed on it - and the knock has to go
     * unanswered there rather than promising fonts that are not there.
     */
    if (strcmp(argv[1], "high") == 0)
        faces_wanted = 2;
    else if (strcmp(argv[1], "low") == 0)
        faces_wanted = 1;
    else if (strcmp(argv[1], "none") == 0)
        faces_wanted = 0;
    else
    {
        printf("Bail out! - no section of the sample ASSIGN.SYS is a %s "
               "screen\n", argv[1]);
        return 1;
    }

    /* Every attribute defaulted, and coordinates in pixels rather than in the
     * normalised space nothing has used since GEM was portable */
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;

    v_opnwk(work_in, &handle, work_out);
    check(handle > 0, 1, "v_opnwk gives a workstation handle");

    /* The knock. vq_gdos answers whether anybody is there and vq_vgdos with
     * which of them it is, so the two have to agree. */
    version = vq_vgdos();
    check(vq_gdos() ? 1 : 0, version != GDOS_NONE,
          "vq_gdos and vq_vgdos agree about whether there is a GDOS");
    /*
     * Which GDOS it is. '_FNT' is the one that loads fonts from files and
     * '_FSM' the one with outlines behind it as well, and which of the two a
     * build has depends on whether it was built with FreeType - so both are
     * accepted here and the answer decides what is asked below. Saying '_FSM'
     * is a promise about the calls above 230, so it had better be true.
     */
    outlines = (version == GDOS_FSM);

    if (faces_wanted)
        check(version == GDOS_FSM || version == GDOS_FNT ? 1 : 0, 1,
              "and say which of the two GDOSes it is");
    else
        /*
         * With no ASSIGN.SYS there are no fonts from files, and whether there
         * is a GDOS at all then depends on whether this build has an outline
         * engine: one with FreeType still has faces to offer and says so,
         * one without has nothing and says that. What it must not say is
         * '_FNT', which is a promise about font files there are none of.
         */
        check(version == GDOS_FSM || version == GDOS_NONE ? 1 : 0, 1,
              "and do not claim font files when no font list was found");

    /*
     * How many faces there are before the fonts are asked for: the system one,
     * and however many outline faces this build turned out to have. It is not
     * a number this file can know, so it is remembered rather than asserted -
     * what matters is that the loaded fonts arrive on top of it.
     */
    faces_before = work_out[10];
    check(faces_before >= 1, 1,
          "a workstation opens knowing at least the system face");

    /*
     * How many faces the application now has that it did not have before, and
     * the outline ones are counted in it - this one number is what an
     * application decides by. Atari Works puts up "Can not find graphics FONTS
     * on your system" when it comes back nought, and on a machine with
     * SpeedoGDOS the outline faces were part of what it counted.
     *
     * So the exact number is not something this file can know. What it can
     * know is that the faces from the sample ASSIGN.SYS are among them, which
     * is counted by name below.
     */
    loaded = vst_load_fonts(handle, 0);
    check(loaded >= faces_wanted, 1,
          "vst_load_fonts answers with at least the faces the section names");

    /* A second time. Fonts are loaded once and that is said by answering with
     * no new faces, which is a different path through the same call. */
    check(vst_load_fonts(handle, 0), 0,
          "asking a second time finds no faces that were not already there");

    vst_unload_fonts(handle, 0);
    check(vst_load_fonts(handle, 0), loaded,
          "and they can be asked for again once they have been unloaded");

    /*
     * And exactly the ones the section names, counted by name rather than by
     * arithmetic. This is the check that says the right section was read: the
     * ST high screen's has two of these and the ST low screen's has one.
     */
    faces = (short)(faces_before + loaded);
    check(samples_among(faces), faces_wanted,
          "and the list holds exactly the fonts that section names");

    if (!faces_wanted)
    {
        /* No font list was found, so none of the sample fonts can be in the
         * list however many outline faces there turn out to be */
        check(samples_among(faces), 0,
              "and no font from a file is in the list");

        v_clswk(handle);
        printf("1..%d\n", n);
        return 0;
    }

    /*
     * The faces, by name. They are looked for rather than assumed to be at a
     * particular place in the list, because what else is in it depends on how
     * the build was made - the outline faces are in there too.
     */
    sans = face_called("Test Sans", faces);
    mono = face_called("Test Mono", faces);

    check(sans, faces_wanted == 2 ? SANS_ID : 0,
          "the face the section names first is in the list under its own name");
    check(mono, MONO_ID, "and so is the other one");

    if (faces_wanted == 2)
    {

        /*
         * And what all of this is for. An application lays a document out from
         * what vqt_extent tells it, so the width of a string in a loaded font
         * is the answer that decides where every word on the page goes. It
         * comes out of the offset table in the .FNT, which is where the byte
         * order had to be right.
         */
        vst_font(handle, SANS_ID);
        vst_point(handle, 8, &extent[0], &extent[1], &extent[2], &extent[3]);

        vqt_extent(handle, " !\"", extent);
        check(extent[2] - extent[0], SANS08_EXTENT,
              "a string in a loaded font is as wide as its offset table says");

        /* The larger size of the same face is a different font in the chain
         * rather than the same one scaled, so it has to be found by asking */
        vst_point(handle, 12, &extent[0], &extent[1], &extent[2], &extent[3]);
        vqt_extent(handle, " !\"", extent);
        check(extent[2] - extent[0], SANS12_EXTENT,
              "and wider at the size above it, that being another font");
    }

    /*
     * And the outline half, when there is one. What can be asked about it here
     * is narrow on purpose: which host face Swiss 721 was set in, and
     * therefore how wide anything comes out, is a fact about the machine this
     * is running on. What holds everywhere is that the face is in the list,
     * that a size can be asked for that no font file has, that the extent
     * grows with the size, and that drawing puts ink where the extent said it
     * would.
     */
    if (outlines)
    {
        short swiss = face_called("Swiss 721", faces);
        short narrow, wide, length;

        check(swiss > 0, 1, "an outline face is in the list under its own name");

        p_intin[0] = swiss;
        speedo(21, 0, 1);               /* vst_font */

        /* Seventeen point, which no .FNT on any disk is, so an answer at all
         * is the outline engine answering */
        p_intin[0] = 17;
        speedo(246, 0, 1);              /* vst_arbpt */
        check(p_intout[0], 17, "and can be asked for a size no font file has");

        length = put_text("Hamburgefonstiv");
        speedo(240, 0, length);         /* vqt_f_extent */
        narrow = p_ptsout[2] - p_ptsout[0];
        check(narrow > 0, 1, "a string in it has a width");

        p_intin[0] = 34;
        speedo(246, 0, 1);
        length = put_text("Hamburgefonstiv");
        speedo(240, 0, length);
        wide = p_ptsout[2] - p_ptsout[0];
        check(wide > narrow, 1, "and a larger one at twice the size");

        /*
         * Drawn, and then read back off the screen. This is the check the rest
         * of them lean on: everything above would pass just as well if v_ftext
         * quietly drew nothing.
         */
        p_intin[0] = 12;
        speedo(246, 0, 1);
        vswr_mode(handle, MD_REPLACE);
        vst_color(handle, 1);
        vst_alignment(handle, 0, 5, &extent[0], &extent[1]);

        length = put_text("Hamburgefonstiv");
        p_ptsin[0] = 20;
        p_ptsin[1] = 40;
        speedo(241, 1, length);         /* v_ftext */

        speedo(240, 0, put_text("Hamburgefonstiv"));
        narrow = p_ptsout[2] - p_ptsout[0];

        check(ink_between(20, 20 + narrow, 40, 40 + 20) > 0, 1,
              "v_ftext puts ink where the extent said the string would be");
        check(ink_between(20 + narrow + 8, 20 + narrow + 60, 40, 40 + 20), 0,
              "and none past the end of it");

        /*
         * And against the left edge, which is not the corner case it looks.
         * The bitmap a string is drawn from starts a character's width before
         * the string does, because a letter may lean left of its own origin -
         * so a string drawn a few pixels in begins at a negative x, and the
         * raster call clips only when the workstation has a clipping rectangle
         * set. Nothing here sets one, which is the ordinary case.
         */
        p_intin[0] = 24;
        speedo(246, 0, 1);
        length = put_text("edge");
        p_ptsin[0] = 2;
        p_ptsin[1] = 100;
        speedo(241, 1, length);

        check(ink_between(2, 60, 100, 130) > 0, 1,
              "a string against the left edge is drawn rather than fatal");

        /*
         * The colour it comes out in. A workstation keeps the text colour as a
         * pen - the register the drawing writes - and the raster call the
         * string is put down with takes a VDI index and maps it to a pen
         * itself, so handing it the pen maps it twice. Black went in and pink
         * came out. Only worth asking on a screen with colours in it.
         */
        if (work_out[13] > 2)
        {
            vst_color(handle, 2);
            length = put_text("colour");
            p_ptsin[0] = 20;
            p_ptsin[1] = 150;
            speedo(241, 1, length);

            check(ink_colour(20, 140, 150, 180), 2,
                  "text asked for in a colour is drawn in that colour");

            vst_color(handle, 1);
        }

        /*
         * And two runs side by side, the second drawn after the first. The
         * bitmap a string goes into used to carry a character's slack at each
         * end so that a letter leaning past its own advance survived, and the
         * blit put the whole rectangle down - so in replace mode the second
         * run wrote background over the tail of the first. It shows while
         * somebody is typing, one run redrawn at a time, and not on a full
         * redraw where everything is painted in order.
         */
        /* Twelve point and well inside the top left corner, because the
         * smallest screen this runs on is 320 by 200 and a check drawn off the
         * edge of it counts no ink either side of the thing it is testing -
         * which is a check that cannot fail rather than one that passes */
        p_intin[0] = 12;
        speedo(246, 0, 1);

        length = put_text("AAAA");
        speedo(240, 0, length);
        narrow = p_ptsout[2] - p_ptsout[0];

        length = put_text("AAAA");
        p_ptsin[0] = 20;
        p_ptsin[1] = 165;
        speedo(241, 1, length);

        wide = (short)ink_between(20, 20 + narrow, 160, 195);
        check(wide > 0, 1, "a run of four letters puts ink down");

        length = put_text("BBBB");
        p_ptsin[0] = (short)(20 + narrow);
        p_ptsin[1] = 165;
        speedo(241, 1, length);

        check(ink_between(20, 20 + narrow, 160, 195), wide,
              "and a run drawn beside it leaves every pixel of it alone");
    }

    v_clswk(handle);

    printf("1..%d\n", n);

    return 0;
}
