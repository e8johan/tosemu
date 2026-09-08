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
 * What comes out of the printer, checked without one.
 *
 * tests/c-print.c checks the printer from inside the emulator, where it can
 * open a workstation, draw on the page and see that a job came out with the
 * right number of pages in it. What it cannot check is the bytes: a PDF page
 * is a run length encoded bitmap, and an application has no way of asking
 * whether the bitmap that came out is the one that went in.
 *
 * That is what this is for, and the encoder is why it is worth a test of its
 * own. It is thirty lines with three boundaries in them - a repeat of exactly
 * 128, a literal stretch of exactly 128, and the end marker being a length
 * that means neither - and every one of them produces a file that still looks
 * like a PDF and still has the right number of pages. So the page is drawn
 * here as a pattern with those cases in it, the job is written to a file, and
 * the file is decoded back and compared with what went in.
 *
 * A5 at thirty six dots to the inch, which is the smallest page these settings
 * can describe. Nothing here is about how large a page is - c-print checks
 * that - and a small one is a comparison somebody can read the failure of.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "printer.h"
#include "settings.h"

/* The emulator's, which this does not link. There is no program, so the
 * emulator's own name is the true answer, and nothing is being said. */
const char *tos_program_name(void)
{
    return "TOSEMU";
}

int verbose;

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

#define JOB "printtest.pdf"

/*
 * The page, as bytes.
 *
 * Every case the run length encoder has a boundary at, in one stream: a repeat
 * of 127, of 128 and of 129, a literal stretch longer than one length byte can
 * describe, two equal bytes in the middle of a literal stretch - which are not
 * worth breaking it for - and three, which are. The rest is a walking value so
 * that a byte landing one place out is a mismatch rather than a coincidence.
 */
static void pattern(unsigned char *into, long length)
{
    long at = 0;
    int repeat[3];
    int i, j;

    repeat[0] = 127;
    repeat[1] = 128;
    repeat[2] = 129;

    for (i = 0; i < 3 && at < length; i++)
        for (j = 0; j < repeat[i] && at < length; j++)
            into[at++] = (unsigned char)(0xa0 + i);

    /* A literal stretch of three hundred, with a pair of equal bytes in it */
    for (i = 0; i < 300 && at < length; i++)
        into[at++] = (unsigned char)((i == 100 || i == 101) ? 0x55 : (i * 7 + 1));

    /* Blank paper, which is the case that matters most: a page of text is
     * mostly this, and it is the one an encoder that could not repeat across a
     * row boundary would still get right */
    for (i = 0; i < 1000 && at < length; i++)
        into[at++] = 0;

    while (at < length)
    {
        into[at] = (unsigned char)(at * 13 + (at >> 8));
        at++;
    }
}

/* Reads the job back, all of it */
static unsigned char *slurp(const char *path, long *length)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data;
    long size;

    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    data = malloc((size_t)size);
    if (!data)
    {
        fclose(f);
        return 0;
    }

    if (fread(data, 1, (size_t)size, f) != (size_t)size)
    {
        free(data);
        fclose(f);
        return 0;
    }

    fclose(f);
    *length = size;

    return data;
}

/* Where something is in the file, searched over a length: a PDF has nul bytes
 * all through it, so nothing that stops at one will find anything */
static long find(const unsigned char *in, long length, const char *what,
                 long from)
{
    long size = (long)strlen(what);
    long i;

    for (i = from; i + size <= length; i++)
        if (memcmp(in + i, what, (size_t)size) == 0)
            return i;

    return -1;
}

/*
 * PDF's run length decoding, which is the other half of what printer.c writes.
 *
 * Written here rather than shared with it on purpose. A decoder that is the
 * encoder read backwards agrees with it about everything including its
 * mistakes; this one is written from the description in the PDF specification,
 * which is what a printer will be reading the file with.
 */
static long decode(const unsigned char *in, long length, unsigned char *into,
                   long room)
{
    long at = 0, out = 0;

    while (at < length)
    {
        int mark = in[at++];

        if (mark == 128)
            break;

        if (mark < 128)
        {
            int count = mark + 1;

            while (count-- > 0 && at < length && out < room)
                into[out++] = in[at++];
        }
        else
        {
            int count = 257 - mark;

            if (at >= length)
                break;

            while (count-- > 0 && out < room)
                into[out++] = in[at];

            at++;
        }
    }

    return out;
}

int main(void)
{
    unsigned short *page;
    unsigned char *want, *got, *job;
    unsigned int palette[2];
    int width, height, words_per_line;
    long length, total, stream, decoded, i;

    /* Whatever is in somebody's home directory is not this test's business */
    settings_ignore_file();

    /*
     * A page size that is not one, which has to be refused rather than turned
     * into a guess: a wrong guess here is a wasted sheet of somebody's paper.
     */
    putenv((char *)"TOSEMU_PRINTER_PAPER=quarto");
    check(printer_page(1, &width, &height, &words_per_line) == 0, 1,
          "a paper size nobody has is refused");

    putenv((char *)"TOSEMU_PRINTER_PAPER=a5");
    putenv((char *)"TOSEMU_PRINTER_DPI=4");
    check(printer_page(1, &width, &height, &words_per_line) == 0, 1,
          "and so is a resolution no page can be drawn at");

    putenv((char *)"TOSEMU_PRINTER_DPI=36");
    page = printer_page(1, &width, &height, &words_per_line);
    check(page != 0, 1, "A5 at thirty six dots to the inch is a page");

    if (!page)
    {
        printf("1..%d\n", n);
        return 1;
    }

    /* A5 is 148 by 210 millimetres, so 209 dots across at this resolution -
     * and the page is 208, a row being a whole number of sixteen pixel words */
    check(width, 208, "as wide as A5 comes to, rounded to a whole word");
    check(height, 297, "and as tall as A5 comes to");
    check(words_per_line, 13, "which is thirteen words to a row");
    check(printer_dpi(), 36, "at the resolution asked for");

    total = (long)width * height / 8;

    want = malloc((size_t)total);
    got = malloc((size_t)total);
    if (!want || !got)
    {
        printf("Bail out! - no room for a page of %ld bytes\n", total);
        return 1;
    }

    pattern(want, total);

    /*
     * Into the page, two bytes to a word, the first of them the one holding
     * the leftmost pixels. The words are in host order, so this is a shift
     * rather than a cast - which is the whole reason printer.c takes them
     * apart the way it does.
     */
    for (i = 0; i < total / 2; i++)
        page[i] = (unsigned short)((want[i * 2] << 8) | want[i * 2 + 1]);

    palette[0] = 0xffffff;      /* paper */
    palette[1] = 0x000000;      /* ink */

    putenv((char *)"TOSEMU_PRINT_FILE=" JOB);
    remove(JOB);

    check(printer_page_out(palette, 2), 1, "the page goes into a job");
    check(printer_job_started(), 1, "which is then a job under way");
    check(printer_job_end(), 1, "and can be finished");
    check(printer_job_started(), 0, "leaving none under way");

    job = slurp(JOB, &length);
    check(job != 0, 1, "and a file where the printer would have been");

    if (!job)
    {
        printf("1..%d\n", n);
        return 1;
    }

    check(find(job, length, "%PDF-1.4", 0), 0, "the file is a PDF");
    check(find(job, length, "/Width 208 /Height 297", 0) > 0, 1,
          "holding a page the size of the paper");
    check(find(job, length, "/BitsPerComponent 1", 0) > 0, 1,
          "in one bit a pixel, which is what one plane comes to");
    check(find(job, length, "<ffffff000000>", 0) > 0, 1,
          "with ink and paper in its palette");

    /*
     * And the page itself. This is the check the rest of the file is scenery
     * for: everything above would still pass with an encoder that dropped a
     * byte at the boundary of a run.
     */
    stream = find(job, length, "/Filter /RunLengthDecode", 0);
    check(stream > 0, 1, "the page is a run length encoded stream");

    stream = find(job, length, "stream\n", stream);
    check(stream > 0, 1, "which has a beginning");

    decoded = decode(job + stream + 7, length - stream - 7, got, total);
    check(decoded, total, "and decodes back to a page of the right size");

    for (i = 0; i < total && i < decoded; i++)
        if (got[i] != want[i])
            break;

    check(i, total, "with every byte of it the byte that was drawn");

    if (i < total && i < decoded)
        printf("# byte %ld came back as %02x rather than %02x\n",
               i, got[i], want[i]);

    remove(JOB);

    printf("1..%d\n", n);

    return 0;
}
