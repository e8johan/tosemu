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
 * The keyboard table, which is how a key becomes a letter again.
 *
 * A menu shortcut on Alternate and a letter arrives as a scan code with
 * nothing typed - that is how TOS says a key was pressed rather than typed -
 * so an application with a menu full of letters has to turn the scan code back
 * into one, and Keytbl is where it gets the alphabet to do it with. An
 * application that reads a pointer out of an answer of nought walks through the
 * exception vectors instead, and every shortcut it has resolves to whatever is
 * lying there.
 *
 * So this asks for the three tables and reads the letters out of them. The
 * letters it expects are the ones on the keyboard a machine came with, which
 * is what tosemu hands over when there is no desktop to ask about the keyboard
 * somebody is really typing on - a test has none, and a run with a compositor
 * and a keyboard laid out some other way would rightly answer differently.
 *
 * Then it hands over a table of its own, which an application setting up its
 * own alphabet is entitled to do, and asks for Bioskeys to put the machine's
 * back.
 */

#include <stdio.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

/* Where the letters this asks about sit on an ST keyboard */
#define KEY_A       (0x1e)
#define KEY_S       (0x1f)
#define KEY_L       (0x26)
#define KEY_C       (0x2e)
#define KEY_TAB     (0x0f)
#define KEY_RETURN  (0x1c)
#define KEY_SPACE   (0x39)
#define KEY_HOME    (0x47)

/* A table of an application's own, which is the whole of what one has to be:
 * a hundred and twenty eight bytes, a key to each */
static char mine[128];

int main(int argc, char **argv)
{
    _KEYTAB *tab, *again;

    tab = (_KEYTAB *)Keytbl((void *)-1L, (void *)-1L, (void *)-1L);

    /* Nought is what this used to answer, and it is the answer an application
     * cannot do anything with: what it does next is follow it */
    check(tab != 0, 1, "there is a keyboard table to be had");
    if (!tab)
    {
        printf("1..%d\n", n);
        return 1;
    }

    check(tab->unshift != 0 && tab->shift != 0 && tab->caps != 0, 1,
          "and all three of its tables are somewhere");

    /*
     * The letters, read out of the third table. That is the one an application
     * matching a menu shortcut wants, capitals being what a menu says, and it
     * is the one GenST2 takes.
     */
    check(tab->caps[KEY_A], 'A', "the key where A is says so");
    check(tab->caps[KEY_S], 'S', "and the key where S is");
    check(tab->caps[KEY_L], 'L', "and the key where L is");
    check(tab->caps[KEY_C], 'C', "and the key where C is");

    /* And the other two, which are the same keys in the other two states */
    check(tab->unshift[KEY_A], 'a', "unshifted it is a small letter");
    check(tab->shift[KEY_A], 'A', "and shifted it is a capital again");

    /* The keys that are not letters and are the same on every keyboard */
    check(tab->unshift[KEY_TAB], 9, "the tab key types a tab");
    check(tab->unshift[KEY_RETURN], 13, "and Return a carriage return");
    check(tab->unshift[KEY_SPACE], ' ', "and the space bar a space");

    /*
     * The cursor keys type nothing, and that is worth asking about: an ST has
     * them where a PC keyboard has its keypad, so a table built by asking the
     * desktop what the key at that number does would put a digit here.
     */
    check(tab->unshift[KEY_HOME], 0, "and the Home key types nothing");

    /* Asking twice is asking about one table rather than making another */
    again = (_KEYTAB *)Keytbl((void *)-1L, (void *)-1L, (void *)-1L);
    check(again == tab, 1, "asking again is the same table");

    /*
     * A table of the application's own. It is handed over and kept as it
     * stands: an application is entitled to write to its own table afterwards
     * and to have the change seen.
     */
    mine[KEY_A] = 'Z';
    again = (_KEYTAB *)Keytbl(mine, (void *)-1L, (void *)-1L);

    check(again->unshift == mine, 1, "a table of one's own is taken");
    check(again->shift[KEY_A], 'A', "and the two not named are left alone");

    /* And Bioskeys puts the machine's own back, which is what an application
     * does before it goes away */
    Bioskeys();
    again = (_KEYTAB *)Keytbl((void *)-1L, (void *)-1L, (void *)-1L);

    check(again->unshift != mine, 1, "Bioskeys gives the machine's own back");
    check(again->unshift[KEY_A], 'a', "and they say what they said before");

    printf("1..%d\n", n);

    return 0;
}
