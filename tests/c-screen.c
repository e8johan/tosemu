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
 * Which screen the machine turned out to have.
 *
 * The ST had three and an application cannot choose between them: it is told,
 * once, and everything it draws is laid out for the answer. A resource is
 * measured in characters, so how many fit across is what decides whether a
 * dialog fits at all - one forty-five characters wide is ordinary on a screen
 * eighty across and hangs off both edges of one that is forty, because the AES
 * centres it and the arithmetic comes out negative.
 *
 * So this asks the two questions an application asks: how large the screen is
 * and how large a character is. The second is not a detail either - it is what
 * every coordinate in a resource is multiplied by - and the two are only
 * consistent when the machine is one of the three the ST actually had.
 *
 * The mode expected is named on the command line rather than compiled in,
 * because the point is that the answer follows what was asked for. The suite
 * runs this once for each screen, and once more with a daemon that was asked
 * for a different one from this program - the daemon is what decides, since
 * the screen has to be one screen for everything sharing it.
 *
 * appl_init before anything, deliberately: it is the call that settles which
 * machine this is, and asking the VDI first would be asking before there is an
 * answer.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>

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

/* The three, and what each of them is. The widths and heights are the pixels
 * an ST put on a television or a monitor; the character sizes are the fonts
 * the VDI picked to go with them, which is the small one except on the screen
 * that has the room for the tall one. */
static const struct {
    const char *name;
    short width, height, colours, wchar, hchar;

    /* How large a pixel is, in thousandths of a millimetre, which is what an
     * application divides by to work out how large a thing is on the glass.
     * The ST's medium resolution screen is the one where they are not square:
     * the same glass carries twice as many across and no more down. */
    short wpixel, hpixel;

    /* And how large the boxes a window frame is built out of come out, which
     * is the AES dividing one of those by the other. It is the only place the
     * shape of a pixel is visible without measuring anything. */
    short wbox, hbox;
} modes[] = {
    { "low",        320, 200,  16, 8,  8, 338, 372, 12, 11 },
    { "medium",     640, 200,   4, 8,  8, 169, 372, 24, 11 },
    { "high",       640, 400,   2, 8, 16, 372, 372, 19, 19 },
    { "tt-medium",  640, 480,  16, 8, 16, 278, 278, 19, 19 },
    { "tt-high",   1280, 960,   2, 8, 16, 278, 278, 19, 19 },

    /*
     * And the two whose size is a rule rather than a number, as they come out
     * with no compositor to ask - which is every test run, and is why they can
     * be checked here at all. The size falls back to the one GEM applications
     * were written for; the planes are what was asked for either way, being
     * the half of those two that does not depend on there being a display.
     *
     * How large they are when there is one is arithmetic rather than a
     * workstation, and bin/screentest checks that.
     */
    { "native-mono",  640, 400,  2, 8, 16, 372, 372, 19, 19 },
    { "native-color", 640, 400, 16, 8, 16, 372, 372, 19, 19 },
};

int main(int argc, char **argv)
{
    short work_in[11], work_out[57];
    short phys, vwk, wchar, hchar, wbox, hbox;
    unsigned int i;
    int which = -1;

    if (argc < 2)
    {
        printf("Bail out! - no screen named to expect\n");
        return 1;
    }

    for (i = 0; i < sizeof modes / sizeof modes[0]; i++)
        if (strcmp(argv[1], modes[i].name) == 0)
            which = (int)i;

    if (which < 0)
    {
        printf("Bail out! - no screen is called %s\n", argv[1]);
        return 1;
    }

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    /* How large a character is, which is the AES's answer and the one every
     * resource is measured against */
    phys = graf_handle(&wchar, &hchar, &wbox, &hbox);
    check(phys > 0, 1, "graf_handle gives the AES's workstation");
    check(wchar, modes[which].wchar, "a character is as wide as the screen says");
    check(hchar, modes[which].hchar, "and as tall");
    check(wbox, modes[which].wbox, "a frame box is as wide as the pixels are");
    check(hbox, modes[which].hbox, "and as tall as a character and a bit");

    /* And how large the screen is, which is the VDI's. work_out holds the
     * largest addressable pixel rather than the count of them. */
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;

    v_opnvwk(work_in, &vwk, work_out);
    check(vwk > 0, 1, "v_opnvwk opens a workstation on it");
    check(work_out[0], modes[which].width - 1, "the screen is as wide as asked");
    check(work_out[1], modes[which].height - 1, "and as tall as asked");
    check(work_out[3], modes[which].wpixel, "its pixels are the right width");
    check(work_out[4], modes[which].hpixel, "and the right height");
    check(work_out[13], modes[which].colours, "with the colours that go with it");

    v_clsvwk(vwk);

    printf("1..%d\n", n);

    appl_exit();

    return fails;
}
