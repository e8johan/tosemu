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
 * Changing the resolution, which moves one screen and not the other.
 *
 * There are two screens here and this is the test that says so. The video
 * hardware's screen is emulated memory, read at whatever shape the shifter's
 * mode register says, and shown as a picture. GEM's screen is a surface of the
 * host's, made once when GEM starts, which the AES lays windows out in and the
 * VDI draws into. A program setting the resolution is setting the first one.
 *
 * So the picture follows and nothing else does: the shape of what is shown
 * changes, and Getrez, the desktop the AES reports and the workstation the VDI
 * opens all go on describing the screen they always did. That is the whole of
 * what is checked here, both ways round - through the XBIOS, and by writing the
 * register, which is how a program that goes past the XBIOS does it.
 *
 * It matters which way it is wrong. An application laid out for a screen it is
 * not drawing on is worse than one that cannot change resolution at all, and
 * the failure is silent - a dialog is centred at a coordinate that is off the
 * edge and nothing says why. So the checks on Getrez and the AES are not
 * decoration around the interesting one; they are the point.
 *
 * This runs on the ST's three screens, the ones the mode register can describe.
 * On any other the machine's own screen is a shape no resolution names and
 * there is nothing to put back, which is a different test.
 *
 * The run says which screen it is on and where the screenshot goes, and this
 * reads the file through GEMDOS like any other.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>
#include <mint/osbind.h>

#define SHOT "rez.ppm"

#define REZREG ((volatile unsigned char *)0xFF8260L)

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

static short width, height, planes;
static unsigned char shot[640 * 400 * 3];

/* The screenshot, which is a binary PPM: a header of three numbers and then
 * three bytes a pixel. It is read for the shape as much as for the pixels, so
 * a header that says anything else is a failure rather than a short read. */
static int read_shot(void)
{
    FILE *f = fopen(SHOT, "rb");
    int w, h, most;

    /*
     * Nothing read leaves no pixels behind, so that a check on one of them
     * cannot pass on what an earlier read put there - or, the first time, on
     * whatever an empty array happens to look like. It is a colour no register
     * can hold, four bits a gun being spread over eight.
     */
    memset(shot, 0xfe, sizeof shot);

    if (!f)
        return 0;

    if (fscanf(f, "P6 %d %d %d", &w, &h, &most) != 3 || w != width
        || h != height)
    {
        fclose(f);
        return 0;
    }

    fgetc(f);
    fread(shot, 3, (size_t)w * h, f);
    fclose(f);

    return 1;
}

static long pixel(int x, int y)
{
    unsigned char *p = shot + 3 * ((long)y * width + x);

    return ((long)p[0] << 16) | ((long)p[1] << 8) | p[2];
}

/* What a colour register looks like on the glass: four bits a gun, each
 * spread over eight */
static long colour_of(int pen)
{
    unsigned short c = (unsigned short)Setcolor(pen, -1);

    return ((long)((c >> 8) & 0xf) * 0x11 << 16)
         | ((long)((c >> 4) & 0xf) * 0x11 << 8)
         | ((c & 0xf) * 0x11);
}

/* The shape of one of the ST's three, which is what the mode register means */
static void shape_of(int rez)
{
    switch (rez)
    {
    case 0:
        width = 320, height = 200, planes = 4;
        break;
    case 1:
        width = 640, height = 200, planes = 2;
        break;
    default:
        width = 640, height = 400, planes = 1;
        break;
    }
}

static short work_in[16], work_out[57];

/* How large the VDI says the screen is, which is the other half of the screen
 * the AES reports and comes from the same surface */
static void workstation_size(short *w, short *h)
{
    short handle = graf_handle(&work_in[0], &work_in[0], &work_in[0],
                               &work_in[0]);
    short i;

    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;

    v_opnvwk(work_in, &handle, work_out);
    *w = work_out[0];
    *h = work_out[1];
    v_clsvwk(handle);
}

int main(int argc, char **argv)
{
    long phys = (long)Physbase();
    short desk_x, desk_y, desk_w, desk_h;
    short after_x, after_y, after_w, after_h;
    short vdi_w, vdi_h, vdi_w_after, vdi_h_after;
    short was_rez, want_rez;
    long ssp;

    (void)argc; (void)argv;

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    was_rez = Getrez();
    if (was_rez > 2)
    {
        printf("Bail out! - this screen is not one of the ST's three\n");
        appl_exit();
        return 1;
    }

    /* Somewhere else to go, and back again. Any of the three that is not the
     * one this machine has. */
    want_rez = (was_rez == 0) ? 2 : 0;

    wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
    workstation_size(&vdi_w, &vdi_h);

    /*
     * A mark in the machine's screen memory, which is what the picture will
     * show. GEM draws into a surface of its own rather than into this, so
     * nothing else ever writes here and a pixel that comes back set is this
     * one. The top bit of the first word is the first pixel of plane nought,
     * which is pen one in every mode.
     */
    *(unsigned short *)phys = 0x8000;

    /* Nothing has taken the hardware over yet, so there is no picture at all */
    shape_of(was_rez);
    Vsync();
    check(read_shot(), 0, "nothing is shown before the resolution is set");

    /*
     * Through the XBIOS, which is where a program that asks politely asks.
     * The base is left where TOS put it: this is a program drawing on the
     * screen it was given, in a resolution of its own choosing, which is the
     * case that used to be indistinguishable from doing nothing.
     */
    Setscreen(-1L, -1L, want_rez);
    shape_of(want_rez);
    Vsync();

    check(read_shot(), 1, "Setscreen's resolution changes the shape of the picture");
    check(pixel(0, 0), colour_of(1), "and what is shown is the screen memory");

    /* And the other screen has not heard about any of it */
    check(Getrez(), was_rez, "Getrez still answers for the screen GEM has");

    wind_get(0, WF_WORKXYWH, &after_x, &after_y, &after_w, &after_h);
    check(after_w, desk_w, "the AES desktop is the width it always was");
    check(after_h, desk_h, "and the height");

    workstation_size(&vdi_w_after, &vdi_h_after);
    check(vdi_w_after, vdi_w, "a workstation opens on the same screen as before");
    check(vdi_h_after, vdi_h, "in both directions");

    /* Back where it was, which puts the picture back to the machine's shape */
    Setscreen(-1L, -1L, was_rez);
    shape_of(was_rez);
    Vsync();
    check(read_shot(), 1, "setting it back puts the picture back");

    /*
     * And the way a program that goes past the XBIOS does it, which reaches
     * the same register and has to do the same thing - a debugger and a demo
     * both write 0xFF8260 themselves rather than trapping for it.
     */
    ssp = Super(0L);
    *REZREG = (unsigned char)want_rez;
    Super((void *)ssp);

    shape_of(want_rez);
    Vsync();
    check(read_shot(), 1, "writing the mode register changes the picture too");
    check(Getrez(), was_rez, "and leaves Getrez alone in its turn");

    ssp = Super(0L);
    *REZREG = (unsigned char)was_rez;
    Super((void *)ssp);

    *(unsigned short *)phys = 0;

    printf("1..%d\n", n);

    appl_exit();

    return fails;
}
