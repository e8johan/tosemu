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
 * Opening the file selector more than once.
 *
 * An application opens it whenever somebody picks Open, so the second time has
 * to be the same dialog as the first. That is not free: the tree comes out of
 * the AES's own resource and is laid out for the character size the screen
 * turned out to have, and the laying out is a list of nudges - the closer, the
 * arrows, the elevator and every one of the twenty-six drive letters is moved
 * from where it already is rather than put where it belongs. Done twice, every
 * nudge happens twice. Nothing looks broken at a glance; the dialog is a
 * little larger each time and its drive letters walk down off the bottom of
 * it.
 *
 * There is nothing in the AES that tells an application where the selector's
 * parts are, so this asks by clicking. The close box is the gadget that moves
 * furthest for what it is - six pixels up the screen per extra laying out, in
 * a box twelve tall - and clicking it walks the path up one folder, which is
 * something the caller can see afterwards because it comes back in the buffer
 * it handed over. So each pass clicks where the close box belongs and then
 * Cancel, and the answer says whether the click found it.
 *
 * Cancel is what makes the failure a failure rather than a hang: it barely
 * moves, so it is hit whether or not the dialog has grown, and a pass whose
 * closer click went nowhere still comes back - with the path it went in with.
 *
 * The clicks are in the Makefile, and where they are is the AES's business
 * rather than this file's, so they are given as pixels and not worked out
 * here. What is checked is that all three passes answer the same, which is the
 * whole of what "the same dialog twice" means.
 *
 * There are three clicks to a pass rather than two, and the first is thrown
 * away. A wait that was answered by the buttons being a certain way rather
 * than by taking something off the queue drops the next thing on the queue
 * when the following wait starts - so the press after the one that closed the
 * dialog before it does not survive into this one. That costs the first pass
 * nothing, because there is no dialog before it, and the extra click is what
 * makes all three passes the same shape whether or not the press is dropped.
 * It lands on the dialog's own background, which does nothing whether it
 * arrives or not.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <gem.h>

/* A TOS path is short, and so is what the selector will take */
#define MAX_PATH  (128)

/* How many times the selector is opened. Three rather than two because the
 * first extra laying out moves the close box six pixels and the second twelve,
 * so a pass that only just found it and a pass that cannot possibly have found
 * it are both here. */
#define PASSES    (3)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

static void check_str(const char *got, const char *want, const char *name)
{
    n++;
    if (strcmp(got, want) == 0)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got [%s], want [%s])\n", n, name, got, want);
}

/* Where the selector is started from, which is the directory this is running
 * in - somewhere that certainly exists and certainly has a parent */
static char start[MAX_PATH];

/* And what came back each time */
static char answer[PASSES][MAX_PATH];

int main(int argc, char **argv)
{
    char path[MAX_PATH], name[16];
    short button;
    int i;

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    /*
     * "C:" and then the directory, which is what Dgetpath hands out - a path
     * on the drive, starting with a backslash and without the drive on the
     * front of it. The mask goes on the end because that is what a path means
     * to the selector: where to look and what to look for.
     */
    strcpy(start, "C:");
    if (Dgetpath(start + 2, 0) != 0)
    {
        printf("Bail out! - cannot tell where this is running\n");
        return 1;
    }

    /* On the separator a TOS path uses, whichever one came back. Which one
     * that is has nothing to do with the selector, and the selector only
     * knows the one. */
    for (i = 0; start[i]; i++)
        if (start[i] == '/')
            start[i] = '\\';

    strcat(start, "\\*.*");

    if (strlen(start) + 4 >= MAX_PATH)
    {
        printf("Bail out! - the path this was built in is too long to test with\n");
        return 1;
    }

    for (i = 0; i < PASSES; i++)
    {
        strcpy(path, start);
        name[0] = '\0';
        button = -1;

        /*
         * The first pass is the plain call and the rest are the one that takes
         * a line of the application's own words, because both go through the
         * same laying out and the one with the words is the one an application
         * reaches for second.
         */
        if (i == 0)
            check(fsel_input(path, name, &button), 1,
                  "the selector opens the first time");
        else
            check(fsel_exinput(path, name, &button, "Pick a file"), 1,
                  "and again with something to say at the top");

        check(button, 0, "and comes back cancelled");

        strcpy(answer[i], path);
    }

    /*
     * The close box was found, so the path came back one folder up from where
     * it went in. A pass that missed it hands back what it was given.
     */
    check(strcmp(answer[0], start) != 0, 1,
          "clicking the close box went up a folder");

    /*
     * And the whole point: the dialog was in the same place every time, so the
     * same click found the same gadget and the same answer came back.
     */
    check_str(answer[1], answer[0], "the second time is the same dialog");
    check_str(answer[2], answer[0], "and so is the third");

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
