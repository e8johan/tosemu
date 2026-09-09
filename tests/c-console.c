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
 * Reading the console, which is the half of it a test can arrange.
 *
 * Which of these calls waits and which does not is the whole of what is
 * checked, and it is what used to be wrong: every one of them answered
 * straight away, so a program that asked for a key got nought - a key nobody
 * pressed - and one that polled for a key got the end of the input reported as
 * a character, over and over, for ever. GenST waits for a keypress this way
 * after it has assembled something, and it is why it never came back.
 *
 * The keys are handed over on standard input by the check line that runs this,
 * and they arrive a little at a time rather than all at once, which is what
 * makes the waiting checkable at all: a call that answered straight away would
 * be answering before the key it is meant to have waited for had been typed.
 * The first three checks each want a key that is not there yet.
 *
 * The last four are what happens once the input has run out - which on a
 * terminal is the end of a pipe and on a real keyboard cannot happen. A call
 * that does not wait has to answer that nothing is there, and go on answering
 * it; a run where the polling ones waited instead would not finish at all,
 * which is what the timeout on the check line is for.
 *
 * The reads happen before anything is printed. Cconin echoes what it read and
 * Cconrs echoes the line, both onto this same standard output, so doing it the
 * other way round would scatter them through the middle of the results.
 */

#include <stdio.h>
#include <mint/osbind.h>

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

/* The LINE structure Cconrs is handed: how much room there is, how much was
 * used, and the characters */
static struct {
    unsigned char maxlen;
    unsigned char actual;
    char buffer[255];
} line;

int main(int argc, char **argv)
{
    long cooked, uncooked, from_bios;
    long waiting_now, bios_now, raw_now;
    long waiting_after, bios_after, raw_after, again_after;

    (void)argc;
    (void)argv;

    /*
     * The three that wait, each for a key that has not been typed yet. Any of
     * them answering without waiting answers with nought, which is a key
     * nobody pressed.
     */
    cooked = Cconin() & 0xff;
    uncooked = Crawcin() & 0xff;
    from_bios = Bconin(2) & 0xff;

    /* The rest of the input arrived with that last one, so now there is
     * something waiting and the two that ask can say so */
    waiting_now = Cconis();
    bios_now = Bconstat(2);
    raw_now = Crawio(0xff) & 0xff;

    /* "hi", two backspaces and "xy", so what is left is what the editing made
     * of it rather than what was typed */
    line.maxlen = sizeof line.buffer;
    Cconrs((char *)&line);

    /* And now there is nothing, and nothing that can ever arrive. The end of
     * the input is not a key: a console that has run out is a keyboard nobody
     * is at, which reports that nothing is waiting rather than reporting a
     * character that was never typed. */
    waiting_after = Cconis();
    bios_after = Bconstat(2);
    raw_after = Crawio(0xff);
    again_after = Crawio(0xff);

    /* A line of its own, so that what the reads above echoed is not in front
     * of the first result */
    printf("\r\n");

    check(cooked, 'a', "Cconin waits for a key that has not been typed yet");
    check(uncooked, 'b', "and so does Crawcin");
    check(from_bios, 'c', "and so does Bconin");

    check(waiting_now, -1, "Cconis says a key is waiting when one is");
    check(bios_now, -1, "and Bconstat agrees");
    check(raw_now, '\r', "Crawio(0xff) hands over the key that was waiting");

    check(line.actual, 2, "Cconrs reads a line as long as what was left of it");
    check(line.buffer[0], 'x', "and the first character survived the editing");
    check(line.buffer[1], 'y', "and so did the second");

    check(waiting_after, 0, "Cconis says nothing waits once the input has ended");
    check(bios_after, 0, "and Bconstat says the same");
    check(raw_after, 0, "Crawio(0xff) answers nothing rather than a character");
    check(again_after, 0, "and answers the same the second time");

    printf("# %d checks, %d failed\n", n, fails);
    printf("1..%d\n", n);

    return fails;
}
