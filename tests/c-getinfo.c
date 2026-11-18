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
 * appl_getinfo and appl_search, the two calls an application uses to find out
 * what the AES it is running on can do and who else is running with it.
 *
 * The AES here reports 1.4, older than either call, so the first thing checked
 * is the way such an AES says it has appl_getinfo anyway: appl_find("?AGI")
 * answering nought. The binding asks the same thing before it makes the call,
 * and gives up without making it if the answer is no.
 *
 * The answers checked are the ones that matter to an application deciding
 * what to do: the fonts it lays text out in, which have to be the AES's own;
 * the resolution and resource format; that appl_search is there and popup
 * menus are not; and that a question with no answer is refused.
 *
 * With no argument the run is alone, and appl_search finds this application
 * and nobody else. Run against a daemon with "wait", it waits for a message;
 * run with "find" alongside one that is waiting, it goes through the list,
 * finds the other one by it, and sends the message to the identifier it was
 * given - which is the proof that the identifier was a real one.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>
#include <mint/osbind.h>

/* The message the waiting one is sent, which is nothing GEM defines */
#define HELLO           (0x4711)

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

static void alone(short me)
{
    short out1, out2, out3, out4, type, id;
    char name[16];

    check(appl_find("?AGI"), 0, "appl_find(\"?AGI\") says appl_getinfo is there");

    /* The high resolution screen the suite runs on lays dialogs out in the
     * 8x16 font, whose vst_height is thirteen */
    out1 = out2 = out3 = out4 = -1;
    check(appl_getinfo(0, &out1, &out2, &out3, &out4), 1,
          "the large font is answered");
    check(out1, 13, "as the 8x16 system font's height");
    check(out2, 1, "and the system font's identifier");
    check(out3, 0, "which is a system font");

    out1 = out2 = out3 = out4 = -1;
    appl_getinfo(1, &out1, &out2, &out3, &out4);
    check(out1, 4, "the small font is the 6x6, whose height is four");

    out1 = out2 = out3 = out4 = -1;
    appl_getinfo(2, &out1, &out2, &out3, &out4);
    check(out1, Getrez() + 2, "the resolution is Getrez's plus two");
    check(out2, 16, "objects can be drawn in sixteen colours");
    check(out3, 0, "colour icons are not drawn in colour");
    check(out4, 1, "and the resource format that has them is read");

    out1 = out2 = out3 = out4 = -1;
    appl_getinfo(4, &out1, &out2, &out3, &out4);
    check(out3, 1, "appl_search is there");
    check(out4, 0, "and rsrc_rcfix is not");

    out1 = out2 = out3 = out4 = -1;
    appl_getinfo(9, &out1, &out2, &out3, &out4);
    check(out2, 0, "popup menus are not there either");

    out1 = out2 = out3 = out4 = -1;
    appl_getinfo(12, &out1, &out2, &out3, &out4);
    check(out1, 0x0008, "AP_TERM is the one AES 4 message that is sent");

    check(appl_getinfo(99, &out1, &out2, &out3, &out4), 0,
          "a question there is no answer to is refused");

    memset(name, 0, sizeof name);
    type = id = -1;
    check(appl_search(APP_FIRST, name, &type, &id), 1,
          "appl_search finds the first application");
    check_text(name, "TEST-C-G", "which is this one, by name");
    check(id, me, "and by identifier");
    check(type, APP_APPLICATION, "and is an application");
    check(appl_search(APP_NEXT, name, &type, &id), 0,
          "and there is nobody else");
}

static void wait_for_hello(void)
{
    short message[8];

    printf("# waiting\n");
    fflush(stdout);

    evnt_mesag(message);
    check(message[0], HELLO, "the one waiting is sent a message by the one "
                             "that found it with appl_search");
}

static void find_the_other(short me)
{
    short type, id, other = -1;
    short message[8];
    char name[16];
    int found = 0, mode = APP_FIRST;

    memset(name, 0, sizeof name);
    while (appl_search(mode, name, &type, &id))
    {
        mode = APP_NEXT;
        found++;
        if (id != me && strcmp(name, "TEST-C-G") == 0)
            other = id;
    }

    check(found, 2, "appl_search goes through both applications");
    check(other >= 0, 1, "and one of them is the other one");

    memset(message, 0, sizeof message);
    message[0] = HELLO;
    message[1] = me;
    check(appl_write(other, sizeof message, message), 1,
          "which a message can be sent to by the identifier it gave");
}

int main(int argc, char **argv)
{
    short me = appl_init();

    if (me < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    if (argc > 1 && strcmp(argv[1], "wait") == 0)
        wait_for_hello();
    else if (argc > 1 && strcmp(argv[1], "find") == 0)
        find_the_other(me);
    else
        alone(me);

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
