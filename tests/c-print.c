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

/* Printing, from the far side of the trap.
 *
 * An application prints by opening a workstation on device 21 - which is where
 * GDOS's ASSIGN.SYS put the printer - drawing the page with the calls it draws
 * a window with, and saying v_updwk when the page is finished. So there is no
 * printing code to test: what there is to test is that the second device
 * exists, that drawing on it goes somewhere other than the screen, and that
 * the screen is exactly as it was afterwards.
 *
 * That last one is the whole risk of the design. The VDI has one set of
 * variables saying where to draw and how large the device is, and a printer
 * call borrows them for the length of the call. A borrow that is not given
 * back leaves an application drawing its windows onto a sheet of paper, and
 * from inside the application that looks like the screen having stopped.
 *
 * The suite runs this with the job going to a file rather than to a printer,
 * so the last few checks read the PDF back and ask how many pages the job it
 * would have sent has in it. Nothing here needs a printer, a queue or a
 * daemon: what is being checked is the emulator, not CUPS.
 *
 * The page size the checks expect is A4 at 150 dots to the inch, which is what
 * the check line in tests/Makefile asks for. A4 is 210 millimetres across, so
 * 1240 dots - and the page is 1232, because a row of a bitmap is a whole
 * number of sixteen pixel words and the width is rounded down to one.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>
#include <mint/osbind.h>

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

/* The page the settings this runs with come to */
#define PAGE_WIDTH  (1232)
#define PAGE_HEIGHT (1753)
#define PAGE_DOT    (169)       /* 25400 thousandths of a mm over 150 */

#define JOB "job.pdf"

static short work_in[11];
static short work_out[57];

static short screen_handle;
static short printer_handle;

static short pixel(short handle, short x, short y)
{
    short pel, index;

    v_get_pixel(handle, x, y, &pel, &index);

    return index;
}

/* A filled rectangle, which is the simplest thing that puts ink anywhere */
static void bar(short handle, short x1, short y1, short x2, short y2)
{
    short pxy[4];

    pxy[0] = x1; pxy[1] = y1;
    pxy[2] = x2; pxy[3] = y2;

    vsf_interior(handle, FIS_SOLID);
    vsf_color(handle, 1);
    v_bar(handle, pxy);
}

/*
 * The tail of the job, which is where a PDF says what is in it: the page tree
 * comes after every page, so /Count is within a few hundred bytes of the end
 * however many pages there are.
 */
static long tail(char *into, long room)
{
    long file = Fopen(JOB, 0);
    long length, at, got;

    if (file < 0)
        return file;

    length = Fseek(0L, (short)file, 2);

    at = length - room;
    if (at < 0)
        at = 0;

    Fseek(at, (short)file, 0);
    got = Fread((short)file, room, into);
    Fclose((short)file);

    return got;
}

/*
 * Whether the job says something, searched over a length rather than up to a
 * nul. What is being read back is the end of a PDF, and a page of a PDF is a
 * compressed bitmap - so there are nul bytes all through it and anything that
 * stops at the first one is reading the last few hundred bytes of a page
 * rather than the few hundred bytes after it.
 */
static int says(const char *in, long length, const char *what)
{
    long size = (long)strlen(what);
    long i;

    for (i = 0; i + size <= length; i++)
        if (memcmp(in + i, what, (size_t)size) == 0)
            return 1;

    return 0;
}

int main(void)
{
    char end[1024];
    long read;
    short i;

    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;            /* coordinates in pixels, not normalised */

    /*
     * The screen first, because the point of most of this is that it survives.
     * A program that prints is a program that was doing something else before
     * it printed.
     */
    work_in[0] = Getrez() + 2;
    v_opnwk(work_in, &screen_handle, work_out);
    check(screen_handle > 0, 1, "the screen opens as a workstation");

    bar(screen_handle, 10, 20, 40, 50);
    check(pixel(screen_handle, 25, 35), 1, "and something drawn on it lands");

    /* And now the printer, on the device number every GEM application asks
     * for */
    work_in[0] = 21;
    v_opnwk(work_in, &printer_handle, work_out);
    check(printer_handle > 0, 1, "the printer opens as a workstation too");
    check(printer_handle != screen_handle, 1,
          "and is a different workstation from the screen");

    /* What it says it is. These are the page rather than the screen, which is
     * the whole of what makes it a second device */
    check(work_out[0], PAGE_WIDTH - 1, "the page is as wide as the paper");
    check(work_out[1], PAGE_HEIGHT - 1, "and as tall");
    check(work_out[3], PAGE_DOT, "a dot on it is a dot of the printer's");
    check(work_out[4], PAGE_DOT, "in both directions");

    /*
     * Drawing on it. The rectangle is past the right hand edge of any screen
     * this suite runs on, so a pixel found there is one on the page and could
     * not be one on the screen.
     */
    bar(printer_handle, 700, 900, 1200, 1700);
    check(pixel(printer_handle, 1000, 1200), 1,
          "drawing on the printer lands on the page");
    check(pixel(printer_handle, 100, 100), 0,
          "and only where it was asked to");

    /*
     * And off the bottom right corner, which is what the printer's clipping is
     * for. A workstation opens with clipping off, and with it off the VDI does
     * not check where it is drawing - the rows below the last one go into
     * whatever the page was allocated in front of. A screen is a size the
     * program was written for; a page is as large as a setting on this machine
     * says, and the same document is one height on A4 and another on Letter.
     */
    bar(printer_handle, PAGE_WIDTH - 10, PAGE_HEIGHT - 10,
        PAGE_WIDTH + 400, PAGE_HEIGHT + 400);
    check(pixel(printer_handle, PAGE_WIDTH - 1, PAGE_HEIGHT - 1), 1,
          "drawing off the page still reaches its last pixel");
    check(pixel(printer_handle, PAGE_WIDTH - 11, PAGE_HEIGHT - 11), 0,
          "and starts where it was asked to");

    /* The screen, which nothing has touched since */
    check(pixel(screen_handle, 25, 35), 1,
          "the screen still has what was drawn on it");
    bar(screen_handle, 60, 20, 70, 30);
    check(pixel(screen_handle, 65, 25), 1,
          "and can still be drawn on after the printer has been");

    /*
     * Two pages, which is what the pair of calls in the middle is for: v_updwk
     * puts the page out and v_clrwk blanks it for the next one. The second
     * page is drawn somewhere else so that it is not the first page twice.
     */
    v_updwk(printer_handle);
    v_clrwk(printer_handle);

    check(pixel(printer_handle, 1000, 1200), 0,
          "v_clrwk takes the page away again");

    bar(printer_handle, 100, 100, 300, 300);
    v_updwk(printer_handle);

    /*
     * And a child, run in the middle of a job.
     *
     * Pexec forks, so the child has a copy of the half written job and of the
     * name of the file it is being written to. Neither is the child's, and
     * finishing either would put a second copy of this document in the queue
     * and take away the file this one is still writing to. The child prints a
     * page of its own, so that what comes out is the count below or three
     * pages - this document with somebody else's on the end of it.
     */
    check(Pexec(0, "test-c-prnchild", "\0", 0L), 0,
          "a child printed a page of its own while this one was printing");

    v_clswk(printer_handle);

    /* The job, which is written when the last workstation on the printer
     * closes - one job for the document rather than one for each page */
    read = tail(end, sizeof end);
    check(read > 0, 1, "closing the printer wrote the job");
    check(says(end, read, "/Count 2"), 1, "and it has the two pages in it");
    check(says(end, read, "%%EOF"), 1, "and is a PDF that was finished");

    /* And the screen is still the screen, with a printer no longer open */
    check(pixel(screen_handle, 65, 25), 1,
          "the screen is untouched by the job going out");

    v_clswk(screen_handle);

    printf("1..%d\n", n);

    return 0;
}
