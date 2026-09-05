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

int main(int argc, char **argv)
{
    short i, id;
    long version;
    char name[33];
    short extent[8];
    int faces_wanted;

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
    check(version, faces_wanted ? GDOS_FNT : GDOS_NONE,
          faces_wanted ? "and say it is the one that loads fonts from files"
                       : "and say there is none, no font list having been found");

    /* Before the fonts are asked for, the only face is the system one. This is
     * what says the count below is the loaded fonts arriving rather than
     * something that was already there. */
    check(work_out[10], 1, "a workstation opens knowing only the system face");

    check(vst_load_fonts(handle, 0), faces_wanted,
          "vst_load_fonts answers with the faces the screen's section names");

    /* A second time. The VDI loads fonts once and says so by answering with no
     * new faces, which is a different path through the same call. */
    check(vst_load_fonts(handle, 0), 0,
          "asking a second time finds no faces that were not already there");

    vst_unload_fonts(handle, 0);
    check(vst_load_fonts(handle, 0), faces_wanted,
          "and they can be asked for again once they have been unloaded");

    if (!faces_wanted)
    {
        /* Nothing else to ask: with no fonts the rest of this is a question
         * about faces that do not exist */
        v_clswk(handle);
        printf("1..%d\n", n);
        return 0;
    }

    /*
     * The faces, by name. Element 1 is the system font, and the loaded ones
     * come after it - which is the order the chain is in, and the reason it is
     * sorted at all.
     */
    id = name_of(2, name);
    check(id, faces_wanted == 2 ? SANS_ID : MONO_ID,
          "the first loaded face is the one the section names first");
    check(strcmp(name, faces_wanted == 2 ? "Test Sans" : "Test Mono"), 0,
          "under the name in its font file");

    if (faces_wanted == 2)
    {
        id = name_of(3, name);
        check(id, MONO_ID, "and the second loaded face after it");
        check(strcmp(name, "Test Mono"), 0, "under its own name as well");

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

    v_clswk(handle);

    printf("1..%d\n", n);

    return 0;
}
