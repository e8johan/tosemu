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
 * What the machine says it is.
 *
 * Every TOS has a header at the bottom of its ROM, and _sysbase points at it.
 * A program that wants to know what it is running on reads the version out of
 * it and branches - MROS, the MIDI kernel Cubase loads, takes an entirely
 * different path through its own startup depending on what it finds there.
 *
 * The failure this is written against is not a wrong version but a missing
 * one. With _sysbase left at nought a program does not get an error: it gets a
 * null pointer, follows it, reads the version out of address 2 - which is the
 * top half of the reset vector, and is nought - and concludes something
 * confidently. Every check below would pass on a machine that answered
 * rubbish, except that they are checks on what the rubbish would have been.
 *
 * And the last of them is the one worth having, though not for the reason it
 * first looks. tosemu answers this question in three places and they do not
 * agree: the header says 3.06 and GEMDOS answers the version it always
 * answered. That is deliberate and it is pinned here so that it stays
 * deliberate - each of the three says what the layer under it implements,
 * which is what a program asking any one of them is really asking. Making
 * them agree by raising the others would have the AES claim calls that are
 * named and not written, and a program that believed it and called one would
 * stop the emulator. See TOS_VERSION in tossystem.c.
 */

#include <stdio.h>
#include <mint/osbind.h>

#define SYSBASE (0x4F2L)

/*
 * What the header says, and what GEMDOS says, which are answers about two
 * different things and are not the same number.
 *
 * 3.06 in the header because that is the oldest TOS whose startup MROS can
 * take - below 3.00 it goes looking for Atari's own ROM code to patch, which
 * is not here. 0x1500 out of Sversion because that is the GEMDOS this has,
 * and saying otherwise would be a claim about which calls exist rather than a
 * label.
 */
#define WANT_TOS    (0x0306)
#define WANT_GEMDOS (0x1500L)

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
        printf("not ok %d - %s (got 0x%lx, want 0x%lx)\n", n, name, got, want);
    }
}

/*
 * Read in supervisor mode and reported afterwards. The system variables are
 * out of reach of a program in user mode, and printf from inside a Supexec'd
 * routine is far more stack than one should spend.
 */
static long base;
static long version;
static long beg;
static long end;

static void look(void)
{
    base = *(volatile long *)SYSBASE;

    if (base)
    {
        version = *(volatile unsigned short *)(base + 0x02);
        beg     = *(volatile long *)(base + 0x08);
        end     = *(volatile long *)(base + 0x0c);
    }
}

int main(int argc, char **argv)
{
    Supexec(look);

    check(base != 0, 1, "_sysbase points at something");

    /*
     * And at somewhere a header could be rather than at whatever was lying in
     * low memory. Below 0x800 is the system variables and the vector table,
     * which is where a null pointer would have led.
     */
    check(base > 0x800L, 1, "and it points above the machine's low memory");

    check(version, WANT_TOS, "which says which TOS this is");

    /*
     * os_beg is the header's own address, so it is the one field whose right
     * answer is known here without being told: a header that does not point at
     * itself is one that has been filled in from the wrong place.
     *
     * Asked as "the same, and not nought" rather than as "the same", because
     * the two being equal is exactly what a machine with no header at all
     * would also say - both of them nought, and the check passing for the one
     * reason it was written to catch.
     */
    check(beg == base && beg != 0, 1, "the header knows where it is");

    /* And where the low memory the system keeps ends, which is where the first
     * program in a machine is loaded */
    check(end, 0x800L, "and where what the system keeps ends");

    /*
     * And GEMDOS still answering for itself. Not the version that shipped with
     * the TOS the header names, on purpose: what this catches is somebody
     * making the two agree without building what agreeing would claim.
     */
    check(Sversion(), WANT_GEMDOS,
          "and GEMDOS answers for what GEMDOS is, not for the header");

    printf("1..%d\n", n);

    (void)argc;
    (void)argv;

    return fails;
}
