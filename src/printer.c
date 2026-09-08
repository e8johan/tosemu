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
 * The printer, on the host side of it. See printer.h for what this is for and
 * src/emuvdi/prndev.c for the VDI that draws on it.
 *
 * Three things happen here and they are worth keeping apart. A sheet of paper
 * becomes a bitmap of a particular size, which is arithmetic. That bitmap
 * becomes a page of a PDF, which is a file format. And the PDF reaches CUPS,
 * which is a program being run.
 *
 * PDF is the format because it is the one every version of CUPS takes. CUPS
 * has accepted PostScript, PNM and half a dozen other things over the years
 * and has stopped accepting some of them again; a PDF holding a page sized
 * image is what its own filters turn everything else into anyway, so handing
 * it one is both the shortest road and the one least likely to move.
 *
 * lp is how it gets there rather than libcups. The library would add a build
 * dependency for printer discovery that lp already does, and lp is on every
 * machine that has CUPS at all because it is part of it. What is lost is
 * asking the printer how large its paper is and where its unprintable margins
 * are, which is why those are settings here - see the note above page_setup.
 */

#include "printer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

#include "settings.h"
#include "tossystem.h"
#include "config.h"

/* Paper ********************************************************************/

/*
 * The sizes worth naming, in tenths of a millimetre.
 *
 * Tenths rather than millimetres because Letter is 215.9 by 279.4 and a paper
 * size that is out by half a millimetre is a page CUPS decides does not match
 * the tray and scales to fit.
 */
static const struct {
    const char *name;
    const char *media;      /* what CUPS calls it */
    int width;
    int height;
} papers[] = {
    { "a3",     "A3",       2970, 4200 },
    { "a4",     "A4",       2100, 2970 },
    { "a5",     "A5",       1480, 2100 },
    { "letter", "Letter",   2159, 2794 },
    { "legal",  "Legal",    2159, 3556 },
};

#define PAPERS (int)(sizeof papers / sizeof papers[0])

/*
 * What the printer turned out to be. Worked out once, because a page is
 * allocated against it and a second workstation opened later has to land on
 * the same sheet as the first.
 */
static struct {
    int settled;

    const char *media;      /* the name to tell CUPS */
    int tenths_w;           /* the sheet, in tenths of a millimetre */
    int tenths_h;
    int dpi;

    int width;              /* and as a bitmap */
    int height;
    int planes;
    int words_per_line;

    unsigned short *page;
} paper;

/* A tenth of a millimetre, in points, which is what a PDF measures in */
#define POINTS_PER_TENTH_MM (72.0 / 254.0)

/*
 * How large the page is, from what the settings say.
 *
 * The paper size is said here rather than asked of the printer, and that is
 * the price of talking to CUPS through lp: the queue knows what is in its
 * tray and there is no way to ask it from a command line without parsing
 * something meant for a person to read. So this is what the sheet is, and
 * -o media= tells CUPS the same thing, which is what stops it deciding the
 * page does not fit and scaling it.
 *
 * Answers 0, having said what was wrong, for settings that do not describe a
 * page - which is worth stopping for rather than printing on a guess, a wrong
 * guess here being a wasted sheet of somebody's paper.
 */
static int page_setup(int planes)
{
    const char *said = setting("TOSEMU_PRINTER_PAPER");
    const char *dpi = setting("TOSEMU_PRINTER_DPI");
    int i;

    paper.media = papers[1].media;      /* A4 */
    paper.tenths_w = papers[1].width;
    paper.tenths_h = papers[1].height;
    paper.dpi = 300;

    if (said)
    {
        for (i = 0; i < PAPERS; i++)
            if (strcasecmp(said, papers[i].name) == 0)
                break;

        if (i == PAPERS)
        {
            fprintf(stderr, "tosemu: %s is not a paper size this knows. The "
                    "ones it does are", said);
            for (i = 0; i < PAPERS; i++)
                fprintf(stderr, "%s %s", i ? "," : "", papers[i].name);
            fprintf(stderr, "\n");
            return 0;
        }

        paper.media = papers[i].media;
        paper.tenths_w = papers[i].width;
        paper.tenths_h = papers[i].height;
    }

    if (dpi)
    {
        paper.dpi = atoi(dpi);

        /*
         * The ceiling is not arbitrary. A VDI coordinate is a signed word, so
         * nothing beyond 32767 dots can be addressed at all, and the tallest
         * sheet here at 600 dots to the inch is already 8400 of them.
         */
        if (paper.dpi < 36 || paper.dpi > 1200)
        {
            fprintf(stderr, "tosemu: %s dots to the inch is not a resolution "
                    "a page can be drawn at\n", dpi);
            return 0;
        }
    }

    /*
     * The width is rounded down to a multiple of sixteen because a row of a
     * bitmap is a whole number of words in both directions: the VDI works a
     * row's length out by dividing the width by sixteen and this works it out
     * by multiplying, and on anything else the two disagree by a word and the
     * drawing walks up the page. The same rule the screen follows.
     */
    paper.width = (int)((long)paper.tenths_w * paper.dpi / 254) & ~15;
    paper.height = (int)((long)paper.tenths_h * paper.dpi / 254);
    paper.planes = planes;
    paper.words_per_line = paper.width / 16 * planes;

    if (paper.width <= 0 || paper.height <= 0 || paper.height > 32767)
    {
        fprintf(stderr, "tosemu: %d by %d tenths of a millimetre at %d dots to "
                "the inch is not a page that can be drawn on\n",
                paper.tenths_w, paper.tenths_h, paper.dpi);
        return 0;
    }

    paper.settled = 1;

    return 1;
}

void *printer_page(int planes, int *width, int *height, int *words_per_line)
{
    if (!paper.settled && !page_setup(planes))
        return 0;

    /* A machine reset can leave a page of the wrong shape behind it: the
     * screen the next program gets need not have as many colours as the one
     * this page was made for, and the printer has as many as the screen */
    if (paper.page && paper.planes != planes)
    {
        free(paper.page);
        paper.page = 0;
    }

    if (!paper.page)
    {
        paper.planes = planes;
        paper.words_per_line = paper.width / 16 * planes;

        paper.page = calloc((size_t)paper.words_per_line * paper.height,
                            sizeof *paper.page);
        if (!paper.page)
        {
            fprintf(stderr, "tosemu: no room for a %d by %d page of %d "
                    "planes\n", paper.width, paper.height, paper.planes);
            return 0;
        }

        if (verbose >= VERBOSE_CONFIG)
        {
            printf("tosemu: the printer's page is %d by %d at %d dots to the "
                   "inch\n", paper.width, paper.height, paper.dpi);
            fflush(stdout);
        }
    }

    if (width)
        *width = paper.width;
    if (height)
        *height = paper.height;
    if (words_per_line)
        *words_per_line = paper.words_per_line;

    return paper.page;
}

int printer_dpi(void)
{
    return paper.settled ? paper.dpi : 0;
}

void printer_page_clear(void)
{
    if (paper.page)
        memset(paper.page, 0,
               (size_t)paper.words_per_line * paper.height * sizeof *paper.page);
}

/* The job, as a PDF ********************************************************/

/*
 * A job is a file being written, and it is a file rather than memory because
 * a document is as many pages as somebody wants to print and lp is handed a
 * path in the end anyway.
 *
 * Objects are numbered as they are written and their offsets kept, because
 * that is what a PDF's cross reference table is. Two numbers are reserved: 1
 * is the catalogue and 2 the page tree, and the page tree is written last
 * because it has to list every page in the document.
 */
#define PDF_CATALOG (1)
#define PDF_PAGES   (2)

static struct {
    FILE *file;

    /* Where it is being written. Long enough for a path somebody set TMPDIR
     * to: the six X's mkstemp fills in have to survive being formatted into
     * this, and a name that lost them is not a name mkstemp will take. */
    char path[512];

    long *offset;           /* where object n starts, indexed from 1 */
    int objects;            /* how many have been given out */
    int room;

    int *page;              /* the object number of each page */
    int pages;
    int page_room;
} job;

static void job_forget(void)
{
    if (job.file)
        fclose(job.file);

    if (job.path[0])
        unlink(job.path);

    free(job.offset);
    free(job.page);

    memset(&job, 0, sizeof job);
}

/* Gives out the next object number and records that it starts here */
static int object_begin(void)
{
    if (job.objects + 1 >= job.room)
    {
        int room = job.room ? job.room * 2 : 64;
        long *grown = realloc(job.offset, (size_t)room * sizeof *grown);

        if (!grown)
            return 0;

        job.offset = grown;
        job.room = room;
    }

    job.objects++;
    job.offset[job.objects] = ftell(job.file);
    fprintf(job.file, "%d 0 obj\n", job.objects);

    return job.objects;
}

static void object_end(void)
{
    fprintf(job.file, "endobj\n");
}

/*
 * A job still open when the program stops, sent.
 *
 * This is registered rather than called from wherever a program ends, because
 * there are three of those and one of them is not a place anybody would think
 * to look: Pterm exits from inside a trap, an unimplemented call stops the
 * emulator and returns through main, and a program that closed its printer
 * workstation properly has already sent its job and leaves this nothing to do.
 * A program that drew five pages meant to print five pages, so what it has is
 * sent rather than left in the temporary directory.
 *
 * A child of fork does not reach this with anything to send: gem_forget makes
 * it let go of the job it inherited before it is a program at all.
 */
static void job_at_exit(void)
{
    printer_job_end();
}

/*
 * Somewhere to write the job.
 *
 * It goes in the temporary directory rather than beside anything, because
 * neither the program being emulated nor the person running it asked for a
 * file - they asked for a printout, and this is on the way to one.
 */
static int job_begin(void)
{
    static int arranged;
    const char *tmp = getenv("TMPDIR");
    int fd;

    if (job.file)
        return 1;

    if (!arranged)
    {
        atexit(job_at_exit);
        arranged = 1;
    }

    memset(&job, 0, sizeof job);

    /* A TMPDIR too long to hold the name is not one to truncate into: what
     * would be left is a name without the six X's on the end, which mkstemp
     * refuses, and the message would be about the wrong thing */
    if (snprintf(job.path, sizeof job.path, "%s/tosemu-print-XXXXXX",
                 (tmp && *tmp) ? tmp : "/tmp") >= (int)sizeof job.path)
        snprintf(job.path, sizeof job.path, "/tmp/tosemu-print-XXXXXX");

    fd = mkstemp(job.path);
    if (fd < 0)
    {
        fprintf(stderr, "tosemu: nowhere to write a print job: %s\n",
                strerror(errno));
        job.path[0] = 0;
        return 0;
    }

    job.file = fdopen(fd, "w+");
    if (!job.file)
    {
        fprintf(stderr, "tosemu: %s could not be opened to write: %s\n",
                job.path, strerror(errno));
        close(fd);
        unlink(job.path);
        job.path[0] = 0;
        return 0;
    }

    /*
     * The second line is a remark holding four bytes above 127, which is how
     * a PDF says it is not a text file. Without it a program that moves the
     * file about may decide it may translate the line endings, and every
     * offset in the cross reference table is then wrong.
     */
    fprintf(job.file, "%%PDF-1.4\n%%\xe2\xe3\xcf\xd3\n");

    job.objects = PDF_PAGES;    /* 1 and 2 are spoken for */
    job.room = 64;
    job.offset = calloc((size_t)job.room, sizeof *job.offset);
    if (!job.offset)
    {
        job_forget();
        return 0;
    }

    job.offset[PDF_CATALOG] = ftell(job.file);
    fprintf(job.file, "%d 0 obj\n<< /Type /Catalog /Pages %d 0 R >>\nendobj\n",
            PDF_CATALOG, PDF_PAGES);

    return 1;
}

int printer_job_started(void)
{
    return job.file != 0;
}

/* Turning the page into what goes in the file ******************************/

/*
 * A row of the page, as the bytes a PDF image is made of.
 *
 * The bitmap is Atari planar - a word of each plane in turn, the highest bit
 * of a word the leftmost pixel - and an image is pixels one after another, so
 * every pixel has to be gathered from as many words as there are planes. The
 * value that comes out is a pen rather than a colour, which is why the image
 * is an indexed one: the palette says what each pen looks like and nothing
 * here has to know.
 *
 * The words are in host byte order, so they are taken apart by shifting rather
 * than by reading the memory as bytes, which would put the two halves of every
 * word the wrong way round on this machine and the right way round on a 68000.
 */
static void row_bytes(const unsigned short *row, unsigned char *into)
{
    int x, plane;
    int accumulated = 0, bits = 0, at = 0;

    for (x = 0; x < paper.width; x++)
    {
        const unsigned short *group = row + (x / 16) * paper.planes;
        int mask = 0x8000 >> (x & 15);
        int pen = 0;

        for (plane = 0; plane < paper.planes; plane++)
            if (group[plane] & mask)
                pen |= 1 << plane;

        accumulated = (accumulated << paper.planes) | pen;
        bits += paper.planes;

        if (bits == 8)
        {
            into[at++] = (unsigned char)accumulated;
            accumulated = 0;
            bits = 0;
        }
    }
}

/*
 * PDF's own run length encoding, which is what keeps a page of text from being
 * a megabyte of mostly white.
 *
 * A length byte under 128 means the next length+1 bytes are literal; one above
 * 128 means the byte after it repeats 257-length times; 128 is the end. There
 * is no compression library involved, which is the point - a filter every PDF
 * reader has, written in half a page, rather than a dependency.
 */
static void runlength_write(FILE *out, const unsigned char *data, size_t n,
                            size_t *written)
{
    size_t i = 0;

    while (i < n)
    {
        size_t run = 1;

        while (i + run < n && data[i + run] == data[i] && run < 128)
            run++;

        if (run >= 2)
        {
            fputc((int)(257 - run), out);
            fputc(data[i], out);
            *written += 2;
            i += run;
            continue;
        }

        /*
         * A literal stretch runs until a repeat worth encoding turns up. Two
         * equal bytes are not worth breaking a literal run for - the break
         * costs a length byte at each end - so it takes three.
         */
        run = 1;
        while (i + run < n && run < 128)
        {
            if (i + run + 2 < n && data[i + run] == data[i + run + 1]
                && data[i + run] == data[i + run + 2])
                break;
            run++;
        }

        fputc((int)(run - 1), out);
        fwrite(data + i, 1, run, out);
        *written += 1 + run;
        i += run;
    }

    fputc(128, out);
    (*written)++;
}

/*
 * The palette, as the string a PDF indexed colour space is built round: three
 * bytes a pen, written in hexadecimal so that nothing in it can be mistaken
 * for the bracket that ends it.
 */
static void palette_write(FILE *out, const unsigned int *palette, int colours)
{
    int i;

    fprintf(out, "<");

    for (i = 0; i < colours; i++)
        fprintf(out, "%02x%02x%02x", (palette[i] >> 16) & 0xff,
                (palette[i] >> 8) & 0xff, palette[i] & 0xff);

    fprintf(out, ">");
}

int printer_page_out(const unsigned int *palette, int colours)
{
    unsigned char *raw;
    size_t row_length, total, written = 0;
    int image, length, content, page, y;
    char stream[128];
    double sheet_w, sheet_h;

    if (!paper.page)
    {
        fprintf(stderr, "tosemu: a page was printed before there was one\n");
        return 0;
    }

    if (!job_begin())
        return 0;

    /*
     * The whole page as image bytes, rather than a row at a time, because the
     * run length encoder is allowed to run a repeat across the end of a row
     * and a blank page is one repeat from corner to corner.
     */
    row_length = (size_t)paper.width * paper.planes / 8;
    total = row_length * paper.height;

    raw = malloc(total);
    if (!raw)
    {
        fprintf(stderr, "tosemu: no room to turn a %d by %d page into an "
                "image\n", paper.width, paper.height);
        job_forget();
        return 0;
    }

    for (y = 0; y < paper.height; y++)
        row_bytes(paper.page + (size_t)y * paper.words_per_line,
                  raw + (size_t)y * row_length);

    /*
     * The image. Its length is an indirect reference because how long a run
     * length encoded stream is is not known until it has been written, and a
     * PDF is written forwards.
     */
    image = object_begin();
    if (!image)
    {
        free(raw);
        job_forget();
        return 0;
    }

    fprintf(job.file, "<< /Type /XObject /Subtype /Image /Width %d /Height %d\n"
            "/ColorSpace [/Indexed /DeviceRGB %d ", paper.width, paper.height,
            colours - 1);
    palette_write(job.file, palette, colours);
    fprintf(job.file, "]\n/BitsPerComponent %d /Filter /RunLengthDecode "
            "/Length %d 0 R >>\nstream\n", paper.planes, job.objects + 1);

    runlength_write(job.file, raw, total, &written);
    free(raw);

    fprintf(job.file, "\nendstream\n");
    object_end();

    length = object_begin();
    if (!length)
    {
        job_forget();
        return 0;
    }

    fprintf(job.file, "%lu\n", (unsigned long)written);
    object_end();

    if (length != image + 1)
    {
        /* The stream said where its length would be, so the two have to be
         * next to each other. Nothing between them writes an object, so this
         * is a mistake in this file rather than something that can happen. */
        fprintf(stderr, "tosemu: the print job's objects came out in the wrong "
                "order\n");
        job_forget();
        return 0;
    }

    /*
     * What draws it: the image, scaled to the whole sheet.
     *
     * The bitmap is a little narrower than the sheet, its width having been
     * rounded down to a whole number of words, so this stretches it back
     * rather than placing it. The stretch is under a part in a thousand and
     * invisible, and what is bought with it is a page whose size is exactly
     * the paper's - which is what stops CUPS deciding the two do not match and
     * shrinking the page to fit inside itself.
     */
    sheet_w = paper.tenths_w * POINTS_PER_TENTH_MM;
    sheet_h = paper.tenths_h * POINTS_PER_TENTH_MM;

    snprintf(stream, sizeof stream, "q %.3f 0 0 %.3f 0 0 cm /Im0 Do Q\n",
             sheet_w, sheet_h);

    content = object_begin();
    if (!content)
    {
        job_forget();
        return 0;
    }

    fprintf(job.file, "<< /Length %d >>\nstream\n%s\nendstream\n",
            (int)strlen(stream), stream);
    object_end();

    page = object_begin();
    if (!page)
    {
        job_forget();
        return 0;
    }

    fprintf(job.file, "<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.3f %.3f]\n"
            "/Resources << /XObject << /Im0 %d 0 R >> >>\n/Contents %d 0 R >>\n",
            PDF_PAGES, sheet_w, sheet_h, image, content);
    object_end();

    if (job.pages + 1 > job.page_room)
    {
        int room = job.page_room ? job.page_room * 2 : 16;
        int *grown = realloc(job.page, (size_t)room * sizeof *grown);

        if (!grown)
        {
            job_forget();
            return 0;
        }

        job.page = grown;
        job.page_room = room;
    }

    job.page[job.pages++] = page;

    return 1;
}

/* Sending it ***************************************************************/

/*
 * Hands the finished file over to CUPS.
 *
 * lp is run rather than a shell, so nothing in a setting can turn into a
 * command: the destination and the title are arguments and stay arguments
 * whatever is in them.
 */
static int spawn_lp(const char *path)
{
    const char *command = setting("TOSEMU_PRINT_COMMAND");
    const char *destination = setting("TOSEMU_PRINTER");
    const char *title = tos_program_name();
    char media[64];
    char *argv[10];
    int argc = 0;
    pid_t child;
    int status;

    if (!command)
        command = "lp";

    snprintf(media, sizeof media, "media=%s", paper.media);

    argv[argc++] = (char *)command;
    if (destination)
    {
        argv[argc++] = (char *)"-d";
        argv[argc++] = (char *)destination;
    }
    argv[argc++] = (char *)"-t";
    argv[argc++] = (char *)((title && *title) ? title : "TOSEMU");
    argv[argc++] = (char *)"-o";
    argv[argc++] = media;
    argv[argc++] = (char *)path;
    argv[argc] = 0;

    child = fork();
    if (child < 0)
    {
        fprintf(stderr, "tosemu: could not run %s: %s\n", command,
                strerror(errno));
        return 0;
    }

    if (child == 0)
    {
        execvp(command, argv);
        fprintf(stderr, "tosemu: could not run %s: %s\n", command,
                strerror(errno));
        _exit(127);
    }

    if (waitpid(child, &status, 0) < 0)
    {
        fprintf(stderr, "tosemu: %s could not be waited for: %s\n", command,
                strerror(errno));
        return 0;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        fprintf(stderr, "tosemu: %s would not print the job\n", command);
        return 0;
    }

    return 1;
}

/* And to a file instead, for a machine with no printer and for a test */
static int copy_to(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    FILE *out;
    char buffer[8192];
    size_t n;

    if (!in)
    {
        fprintf(stderr, "tosemu: %s could not be read back: %s\n", from,
                strerror(errno));
        return 0;
    }

    out = fopen(to, "wb");
    if (!out)
    {
        fprintf(stderr, "tosemu: %s could not be written: %s\n", to,
                strerror(errno));
        fclose(in);
        return 0;
    }

    while ((n = fread(buffer, 1, sizeof buffer, in)) > 0)
        if (fwrite(buffer, 1, n, out) != n)
        {
            fprintf(stderr, "tosemu: %s could not be written: %s\n", to,
                    strerror(errno));
            fclose(in);
            fclose(out);
            return 0;
        }

    fclose(in);

    if (fclose(out) != 0)
    {
        fprintf(stderr, "tosemu: %s could not be written: %s\n", to,
                strerror(errno));
        return 0;
    }

    return 1;
}

int printer_job_end(void)
{
    const char *file = setting("TOSEMU_PRINT_FILE");
    long xref;
    int i, ok;

    if (!job.file)
        return 1;       /* nothing was printed, which is not a failure */

    /* The page tree, which is the object every page named as its parent and
     * which could not be written until they had all been written */
    job.offset[PDF_PAGES] = ftell(job.file);
    fprintf(job.file, "%d 0 obj\n<< /Type /Pages /Count %d /Kids [",
            PDF_PAGES, job.pages);
    for (i = 0; i < job.pages; i++)
        fprintf(job.file, "%s%d 0 R", i ? " " : "", job.page[i]);
    fprintf(job.file, "] >>\nendobj\n");

    xref = ftell(job.file);

    fprintf(job.file, "xref\n0 %d\n", job.objects + 1);
    fprintf(job.file, "0000000000 65535 f \n");
    for (i = 1; i <= job.objects; i++)
        fprintf(job.file, "%010ld 00000 n \n", job.offset[i]);

    fprintf(job.file, "trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n"
            "%ld\n%%%%EOF\n", job.objects + 1, PDF_CATALOG, xref);

    if (fclose(job.file) != 0)
    {
        fprintf(stderr, "tosemu: the print job could not be written: %s\n",
                strerror(errno));
        job.file = 0;
        job_forget();
        return 0;
    }

    job.file = 0;

    ok = file ? copy_to(job.path, file) : spawn_lp(job.path);

    if (ok && verbose >= VERBOSE_CONFIG)
    {
        printf("tosemu: %d page%s went to %s\n", job.pages,
               job.pages == 1 ? "" : "s", file ? file : "the printer");
        fflush(stdout);
    }

    job_forget();

    return ok;
}

void printer_job_forget(void)
{
    /*
     * Not job_forget, which unlinks the file: this is a child of fork letting
     * go of the parent's job, and the parent is still writing to that file. So
     * the copy of the handle is closed and the rest is simply forgotten.
     */
    if (job.file)
        fclose(job.file);

    free(job.offset);
    free(job.page);

    memset(&job, 0, sizeof job);

    /* And the page, which is the parent's drawing. A child that prints starts
     * on a blank one, the way a program started on its own does. */
    if (paper.page)
        printer_page_clear();
}
