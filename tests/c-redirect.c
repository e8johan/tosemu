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
 * The console as a handle, which is what it is on TOS.
 *
 * Handles 0 and 1 start out as CON:, the device, which is minus one, and the
 * console calls are reads and writes on them. So a program that forces
 * handle 1 onto a file has sent Cconws into the file, and one that forces
 * handle 0 onto a file reads its keys out of it - which is how a shell
 * redirected a program's input and output, and how a program captured what
 * another one printed. Minus one is a handle wherever GEMDOS takes one, and
 * forcing it back onto handle 1 puts the console back.
 *
 * And the console's keys and a read of handle 0 come from the same place: a
 * program told by Cconis that a key is waiting finds that key in Fread(0). The
 * check line hands this two bytes on standard input for that.
 *
 * Nothing is printed while handle 1 is somewhere else, since the C library
 * writes through it as well; what is found is kept and printed afterwards.
 * Two lines are written straight to the console instead, as TAP comments, and
 * the check line looks for them in the output.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>

#define OUTFILE     "REDIR.OUT"
#define INFILE      "REDIR.IN"

/* What a console read answers once the file it was forced onto has ended */
#define EOF_INDICATOR (0xff1aL)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

static void check_text(const char *got, const char *want, const char *name)
{
    n++;
    if (strcmp(got, want) == 0)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got '%s', want '%s')\n", n, name, got, want);
}

/* The whole of a small file, as a string */
static void slurp(const char *name, char *into, int room)
{
    long h = Fopen(name, 0);
    long got;

    into[0] = 0;
    if (h < 0)
        return;

    got = Fread((short)h, room - 1, into);
    if (got >= 0)
        into[got] = 0;
    Fclose((short)h);
}

static void spill(const char *name, const char *what)
{
    long h = Fcreate(name, 0);

    if (h < 0)
        return;

    Fwrite((short)h, strlen(what), what);
    Fclose((short)h);
}

int main(int argc, char **argv)
{
    static const char aside[] = "# written to minus one while 1 was a file\r\n";
    static const char back[] = "# Cconws is on the console again\r\n";

    long con, out, in, forced, aux;
    long key, necin, waiting, after, ended;
    long piped_waiting, piped_read, piped_end;
    char captured[64], piped[8];
    struct
    {
        unsigned char max, actual;
        char text[32];
    } line;
    int tries;

    /* Everything the C library has is out before handle 1 moves */
    fflush(stdout);

    con = Fopen("CON:", 0);

    /*
     * Output. Into a file and back, with a line to the console by its own
     * handle in between.
     */
    out = Fcreate(OUTFILE, 0);
    forced = Fforce(1, (short)out);
    Cconws("ab");
    Cconout('c');
    Crawio('d');
    Fwrite(-1, sizeof aside - 1, aside);
    Fforce(1, -1);
    Fclose((short)out);

    Cconws(back);

    slurp(OUTFILE, captured, sizeof captured);
    Fdelete(OUTFILE);

    /*
     * Input, out of a file. A key, then a line - which ends at the carriage
     * return and takes the line feed with it - then a key past the line, and
     * then the end.
     */
    spill(INFILE, "kline\r\nz");
    in = Fopen(INFILE, 0);
    Fforce(0, (short)in);

    waiting = Cconis();
    key = Cconin();
    line.max = sizeof line.text;
    line.actual = 0;
    Cconrs((char *)&line);
    line.text[line.actual] = 0;
    necin = Cnecin();
    after = Cconis();
    ended = Crawcin();

    Fforce(0, -1);
    Fclose((short)in);
    Fdelete(INFILE);

    /*
     * And the console's own input, which the check line pipes in: told a key
     * is waiting, the read of handle 0 has it. Cconis is asked until it says
     * yes, the bytes being on their way rather than certainly there.
     */
    for (tries = 0; tries < 100000 && !Cconis(); tries++)
        ;
    piped_waiting = Cconis();
    memset(piped, 0, sizeof piped);
    piped_read = Fread(0, 2, piped);
    piped_end = Fread(0, 2, piped + 4);

    aux = Fwrite(-2, 1, "x");

    check(con & 0xffff, 0xffff, "CON: opens as the console's handle, minus one");
    check(forced, 0, "handle 1 can be forced onto a file");
    check_text(captured, "abcd",
               "and Cconws, Cconout and Crawio then write into the file");

    check(waiting, -1, "Cconis says there is more in a file forced onto 0");
    check(key, 'k', "Cconin reads its key out of the file");
    check_text(line.text, "line",
               "Cconrs reads a line of it, up to the carriage return");
    check(necin, 'z', "and takes the line feed with it");
    check(after, 0, "Cconis says there is no more at the end of the file");
    check(ended, EOF_INDICATOR, "and a key read past the end is 0xff1a");

    check(piped_waiting, -1, "Cconis says a key is waiting on the console");
    check(piped_read, 2, "Fread of handle 0 then reads it, and the next one");
    check_text(piped, "xy", "so the key Cconis saw is not lost");
    check(piped_end, 0, "and the end of the input is nought");

    check(aux, -37, "AUX: is refused, there being no serial port");

    printf("1..%d\n", n);

    return 0;
}
