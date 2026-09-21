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
 * What a key types, when the keyboard is not an American one.
 *
 * The host says what a key means in Unicode, because the layout belongs to the
 * desktop, and an ST has its own alphabet above 127 - so a Swedish keyboard
 * saying U+00E5 has to reach the application as 0x86, which is where an ST
 * keeps a with a ring. That conversion did not happen: the character was taken
 * only when the host wrote it as a single byte, which UTF-8 never does for a
 * letter outside ASCII, so every accented key arrived with a scan code and no
 * character at all. Atari Works showed it - typing a Nordic letter into a
 * document put something else there.
 *
 * The keys come from TOSEMU_KEYS, which is the check line standing in for a
 * person, and it is a setting so it is host text the same way a keypress is.
 * That is what makes this reachable from here at all: a compositor is not
 * something a test can arrange, so what a real keypress is turned into is
 * checked on the host, in bin/scraptest, over the same conversion.
 *
 * Every wait here has a timer on it as well. A key that never arrives would
 * otherwise stop the emulator with nothing said, and a test that hangs is
 * worth less than one that fails - so a missing key comes back as MU_TIMER and
 * is reported as a key of nought.
 */

#include <stdio.h>
#include <gem.h>

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

/* The next key, or nought if none turned up before the timer did */
static short next_key(void)
{
    short message[8];
    short mx = 0, my = 0, mb = 0, ks = 0, kr = 0, br = 0;
    short what;

    what = evnt_multi(MU_KEYBD | MU_TIMER, 0, 0, 0,
                      0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0,
                      message, 500,
                      &mx, &my, &mb, &ks, &kr, &br);

    return (what & MU_KEYBD) ? kr : 0;
}

/* What was typed, which is the low half of the key. The high half is the scan
 * code, and an injected key has none. */
static short typed(void)
{
    return (short)(next_key() & 0xff);
}

int main(int argc, char **argv)
{
    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    /*
     * The order is the order of TOSEMU_KEYS in the check line: the six Nordic
     * letters, then a plain one, then a Return.
     */
    check(typed(), 0x86, "a Nordic keyboard types an a with a ring");
    check(typed(), 0x84, "and an a with two dots");
    check(typed(), 0x94, "and an o with two dots");
    check(typed(), 0x8F, "and the capitals of all three");
    check(typed(), 0x8E, "the second one");
    check(typed(), 0x99, "and the third");

    /* A letter that needs no conversion, which a broken one still gets right,
     * and is here so that the six above cannot be passed by a table that
     * changes everything */
    check(typed(), 'a', "a letter in ASCII arrives as itself");

    /* Return carries a scan code as well, and a dialog reads that half rather
     * than the character */
    check(next_key(), 0x1c0d, "and Return arrives with its scan code on it");

    /*
     * And the keys that type nothing, which are the ones a setting could not
     * ask for at all until they were given names.
     *
     * They matter because a run of characters can only ever add to the end of
     * what it has already added: nothing typed forwards moves a caret back, so
     * an editor driven from a setting could not be made to go anywhere. What
     * arrives is the scan code in the high half and nothing in the low one -
     * which is the whole of how an application tells them apart - and the few
     * that do type something answer with it, Backspace being the case here.
     */
    check(next_key(), 0x4b00, "\\{left} arrives as the cursor left key");
    check(next_key(), 0x4800, "and \\{up} as the one above it");
    check(next_key(), 0x4700, "and \\{home} as Clr Home");
    check(next_key(), 0x3b00, "and \\{f1} as the first function key");
    check(next_key(), 0x6100, "and \\{undo} as Undo, which an ST has");
    check(next_key(), 0x0e08, "and Backspace types the character it types");

    /*
     * A name nobody has presses nothing at all, and says so where a person
     * will see it - the check line greps for that. Silently pressing nothing
     * is the failure worth guarding against: from the far side it looks
     * exactly like the application ignoring the key.
     */
    check(next_key(), 0x1c0d, "a key with no such name presses nothing");

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
