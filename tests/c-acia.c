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
 * TOS's ACIA interrupt handler, looked at the way a program looks at it.
 *
 * Every other test here asks the machine to do something and checks what came
 * of it. This one reads TOS itself, because that is what the programs this was
 * written for do: MROS, the MIDI kernel Cubase loads, takes the vector at
 * 0x118, walks back along the XBRA chain, and then reads forward through the
 * handler as instructions until it finds the one that tests the keyboard ACIA
 * - so that it can call TOS's handler as a subroutine starting there.
 *
 * What it used to find was a two byte return-from-exception and then whatever
 * happened to be mapped next, and its scan has no end to it: no bound, no
 * give-up, just a loop until the instruction turns up. It ran off into
 * unmapped memory and stopped the emulator.
 *
 * So this walks it the same way and holds the machine to all of it: that the
 * vector points at something, that reading forward finds the instruction, that
 * the instruction is the keyboard ACIA's and not some other byte that happened
 * to look like it, and that calling in at that point comes back.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>

#define ACIA_VECTOR (0x118L)

/* What MROS looks for: tst.b with a sixteen bit absolute address, and the
 * address being the keyboard ACIA's control register */
#define TST_B_ABS   (0x4A38)
#define IKBD_STATUS (0xFC00)

/*
 * How far to look before giving up.
 *
 * MROS looks as far as it takes and has no bound at all - that is the whole
 * failure this was written against. A test cannot do that: reading forward for
 * ever off the end of a handler is how the emulator stopped, and a test that
 * stops the emulator says nothing. tosemu's handler is twenty eight bytes and
 * the instruction is twenty into it, so this is the handler's own length.
 */
#define LOOK_AT_MOST (28)

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

/* Gathered in supervisor mode, because the vector table is out of reach of a
 * program in user mode and so is most of what this then reads */
static long vector;
static int  found_at = -1;
static long operand;
static long called_back;

/* What the handler does when it is entered at the instruction MROS enters it
 * at: it has to be the start of something that returns, because what MROS does
 * with the address is jsr to it */
static void call_it(void)
{
    void (*routine)(void) = (void (*)(void))(vector + found_at);

    routine();

    called_back = 1;
}

static void look(void)
{
    const volatile unsigned short *word;
    int i;

    vector = *(volatile long *)ACIA_VECTOR;

    /*
     * Somewhere a handler could be. Below 0x800 is the vector table and the
     * system variables, which is where a vector holding nothing would point,
     * and 0xFF0000 up is the hardware registers. The BIOS RAM in between is
     * where this machine keeps the things it hands out.
     */
    if (vector <= 0x800L || vector >= 0xFF0000L)
        return;

    word = (const volatile unsigned short *)vector;

    for (i = 0; i < LOOK_AT_MOST / 2; i++)
    {
        if (word[i] == TST_B_ABS)
        {
            found_at = i * 2;
            operand = word[i + 1];
            break;
        }
    }
}

int main(int argc, char **argv)
{
    Supexec(look);

    check(vector != 0, 1, "the ACIA vector points at something");

    /*
     * And at something longer than a bare return from exception. That is what
     * it held before - the address every unclaimed vector in the table is
     * filled with - and a program reading it as code found two bytes and then
     * whatever came next in the memory map.
     */
    check(found_at >= 0, 1, "and what it points at has the keyboard test in it");

    check(operand, IKBD_STATUS, "which tests the keyboard ACIA in particular");

    /*
     * And that entering it there works, which is a different claim from the
     * instruction being present: MROS does not jump to the handler, it calls
     * into the middle of it and expects to come back.
     */
    if (found_at >= 0)
    {
        Supexec(call_it);
        check(called_back, 1, "and it can be called there and returns");
    }
    else
        check(0, 1, "and it can be called there and returns");

    printf("# the handler is at 0x%lx, the keyboard test %d bytes into it\n",
           vector, found_at);

    printf("1..%d\n", n);

    (void)argc;
    (void)argv;

    return fails;
}
