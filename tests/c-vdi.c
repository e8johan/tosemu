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

/* Drawing, from the far side of the trap.
 *
 * emuvdi's own test checks that the ported VDI draws, by calling it directly
 * on the host. This checks the rest of the way: that an application in the
 * emulated machine can open a workstation, draw into it, and read back what
 * it drew, with the parameter block crossing between the two.
 *
 * It opens a physical workstation, which is what a program with no AES under
 * it has to do. That is how a test reaches the VDI while there is no AES to
 * ask for a window, and not a promise that programs which take over the whole
 * screen are meant to work.
 *
 * v_get_pixel is what makes this a test rather than a demonstration: the
 * answer comes back out of the surface, so a call that quietly did nothing is
 * told apart from one that drew.
 *
 * On the ST's low resolution screen, which the suite asks for by setting
 * TOSEMU_SCREEN. That is not the screen an application would want - it is
 * forty characters across and a GEM dialog does not fit on it - but half of
 * what the VDI does is decide which planes to light, and the screen a GEM
 * application wants has one plane and two colours. The sizes below are that
 * screen's, so they say which one this asked for as well as checking it.
 */

#include <stdio.h>
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

/* The colour index at a point, which is the second of the two answers
 * v_get_pixel gives: the first is the value in the planes. */
static short pixel(short x, short y)
{
    short pel, index;

    v_get_pixel(handle, x, y, &pel, &index);

    return index;
}

int main(int argc, char **argv)
{
    short pxy[8];
    short i;

    /* Every attribute defaulted, and coordinates in pixels rather than in the
     * normalised space nothing has used since GEM was portable */
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;

    v_opnwk(work_in, &handle, work_out);
    check(handle > 0, 1, "v_opnwk gives a workstation handle");

    /* What the workstation says it is, which is the screen this was asked to
     * run on. work_out holds the largest addressable pixel rather than the
     * count of them. */
    check(work_out[0], 319, "the workstation is 320 pixels across");
    check(work_out[1], 199, "the workstation is 200 pixels down");
    check(work_out[13], 16, "the workstation has 16 colours");

    vswr_mode(handle, MD_REPLACE);

    /* A filled rectangle, and then the corners of it */
    vsf_interior(handle, FIS_SOLID);
    vsf_color(handle, 1);
    pxy[0] = 10; pxy[1] = 20;
    pxy[2] = 40; pxy[3] = 50;
    v_bar(handle, pxy);

    check(pixel(10, 20), 1, "v_bar filled its top left corner");
    check(pixel(40, 50), 1, "v_bar filled its bottom right corner");
    check(pixel(25, 35), 1, "v_bar filled its middle");
    check(pixel(9, 20), 0, "v_bar left the pixel outside it alone");
    check(pixel(41, 50), 0, "v_bar stopped at its right edge");
    check(pixel(10, 51), 0, "v_bar stopped at its bottom edge");

    /* A second bar in another colour, to show the planes are separate: 5 is
     * planes 0 and 2, so a colour that came back as 1 or 4 would mean one of
     * them was not written */
    vsf_color(handle, 5);
    pxy[0] = 60; pxy[1] = 20;
    pxy[2] = 80; pxy[3] = 50;
    v_bar(handle, pxy);
    check(pixel(70, 35), 5, "a bar in colour 5 lights planes 0 and 2");

    /* A line, which is drawn by different code from a fill */
    vsl_color(handle, 3);
    vsl_type(handle, 1);
    pxy[0] = 100; pxy[1] = 100;
    pxy[2] = 200; pxy[3] = 100;
    v_pline(handle, 2, pxy);

    check(pixel(100, 100), 3, "v_pline drew its first point");
    check(pixel(150, 100), 3, "v_pline drew its middle");
    check(pixel(200, 100), 3, "v_pline drew its last point");
    check(pixel(150, 101), 0, "v_pline drew one row only");

    /* Clipping, which the workstation applies rather than the caller */
    vs_clip(handle, 1, pxy);        /* pxy is still 100,100 - 200,100 */
    vsf_color(handle, 2);
    pxy[0] = 90;  pxy[1] = 90;
    pxy[2] = 210; pxy[3] = 110;
    v_bar(handle, pxy);
    check(pixel(150, 100), 2, "a clipped bar drew inside the rectangle");
    check(pixel(150, 105), 0, "a clipped bar drew nothing outside it");
    vs_clip(handle, 0, pxy);

    /*
     * Text, which reaches the fonts and the blit written for this.
     *
     * Colour 1 is black and colour 0 is white, which is the way round GEM has
     * it and the opposite of what is easy to assume. Drawing colour 0 text in
     * replace mode would fill the cell with colour 0 and draw the glyph in
     * colour 0 as well, which is a solid white block rather than a letter.
     *
     * So a cell has to be checked for both: some of it drawn and some of it
     * not. Asking only whether anything was drawn passes on a solid block,
     * which is how a letter that was never a letter went unnoticed.
     */
    vswr_mode(handle, MD_REPLACE);
    vst_color(handle, 1);
    v_gtext(handle, 10, 150, "A");
    {
        short set = 0, clear = 0;
        short cx, cy;

        for (cy = 143; cy <= 150; cy++)
            for (cx = 10; cx < 18; cx++)
                if (pixel(cx, cy))
                    set++;
                else
                    clear++;

        check(set > 0, 1, "v_gtext drew some of the cell it was given");
        check(clear > 0, 1, "and left the rest of it alone, so it is a glyph");
    }

    /*
     * A raster copy, which is the one place a bitmap crosses between the
     * machine and the emulator. The application's is 68000 memory, so its
     * words are the other way round from the screen's, and getting that wrong
     * shows up as a picture that is right in shape and wrong in every word.
     */
    {
        static short bits[16 * 4];      /* 16 rows, one word, four planes */
        MFDB screen_fdb, off;
        short pxy2[8];

        screen_fdb.fd_addr = 0L;        /* the screen */
        screen_fdb.fd_w = 320;
        screen_fdb.fd_h = 200;
        screen_fdb.fd_wdwidth = 320 / 16;
        screen_fdb.fd_stand = 0;
        screen_fdb.fd_nplanes = 4;

        off.fd_addr = bits;
        off.fd_w = 16;
        off.fd_h = 16;
        off.fd_wdwidth = 1;
        off.fd_stand = 0;
        off.fd_nplanes = 4;

        check(pixel(64, 24), 5, "the patch about to be copied is colour 5");
        check(pixel(79, 39), 5, "and so is its far corner");

        /* A patch of the colour 5 bar, off the screen and into our own bitmap */
        pxy2[0] = 64; pxy2[1] = 24;
        pxy2[2] = 79; pxy2[3] = 39;
        pxy2[4] = 0;  pxy2[5] = 0;
        pxy2[6] = 15; pxy2[7] = 15;
        vro_cpyfm(handle, S_ONLY, pxy2, &screen_fdb, &off);

        /*
         * The first word of each plane of the first row, which is the whole
         * of a sixteen pixel run of one colour.
         *
         * The planes do not hold the colour an application asked for. The VDI
         * maps a colour index onto a hardware pen, and its table starts
         * 0, 15, 1, 2, 4, 6, so index 5 is pen 6, which is planes 1 and 2.
         * Checking the raw words rather than asking v_get_pixel is the point:
         * v_get_pixel maps back again, so it would agree whatever order the
         * words arrived in.
         */
        check((unsigned short)bits[0], 0x0000, "vro_cpyfm brought plane 0 back");
        check((unsigned short)bits[1], 0xffff, "vro_cpyfm brought plane 1 back");
        check((unsigned short)bits[2], 0xffff, "vro_cpyfm brought plane 2 back");
        check((unsigned short)bits[3], 0x0000, "vro_cpyfm brought plane 3 back");

        /* And back the other way, into a corner that was empty */
        pxy2[0] = 0;   pxy2[1] = 0;
        pxy2[2] = 15;  pxy2[3] = 15;
        pxy2[4] = 200; pxy2[5] = 100;
        pxy2[6] = 215; pxy2[7] = 115;
        vro_cpyfm(handle, S_ONLY, pxy2, &off, &screen_fdb);
        check(pixel(208, 108), 5, "and copied it back to the screen");
        check(pixel(216, 108), 0, "stopping where it was told to");
    }

    v_clrwk(handle);
    check(pixel(25, 35), 0, "v_clrwk emptied the screen");

    v_clswk(handle);

    printf("1..%d\n", n);

    return 0;
}
