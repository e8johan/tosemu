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

/* Going into supervisor mode and coming back out of it.
 *
 * What this is really about is the stack. A 68000 keeps two stack pointers in
 * the one register and swaps them whenever the S bit moves, so a switch that
 * does no more than move the bit leaves the caller standing on the system's
 * stack rather than on its own - with none of what it had put there, and with
 * the address it is to return to somewhere it can no longer reach. Every check
 * below that compares two stack pointers is asking about that.
 *
 * Which is why nothing is said until the mode has been given back. A caller
 * whose stack has been taken away has a few words to spend before it runs off
 * the end of whatever it was handed, and printf spends far more than that: a
 * check reported from in there stops the emulator instead of failing, and a
 * test that stops says nothing about what it found. So the run gathers what it
 * sees, comes back out, and only then reports.
 */

#include <stdio.h>
#include <mint/osbind.h>

/* _bootdev, the drive the machine came up from. Any of the system variables
 * would do - what matters is that it is one, since the whole area they live in
 * is out of reach of a program running in user mode. */
#define BOOTDEV (0x446L)

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

/* Where the stack is standing. The value itself means nothing - it is a
 * different number in every frame - so every check below compares two of them
 * taken the same way rather than looking at one. */
static long stack_pointer(void)
{
    long sp;

    __asm__ volatile ("move.l %%sp,%0" : "=d" (sp));

    return sp;
}

int main(int argc, char **argv)
{
    long before, inside, after;
    long user_mode, super_mode, back_again;
    long bootdev;
    long ssp;

    user_mode = Super(1L);

    before = stack_pointer();
    ssp = Super(0L);
    inside = stack_pointer();

    /* Read in here rather than reported from in here: this is the one check
     * that says the mode is a mode and not a flag somebody set, and a program
     * in user mode reading it stops the emulator */
    super_mode = Super(1L);
    bootdev = *(volatile unsigned char *)BOOTDEV;

    /* Whatever Super answered with is what puts things back, and putting them
     * back is the whole of what the answer is for */
    Super((void *)ssp);
    after = stack_pointer();
    back_again = Super(1L);

    check(user_mode, 0, "a program starts in user mode");
    check(inside, before, "Super leaves the caller standing on its own stack");
    check(super_mode, 1, "and in supervisor mode");
    check(bootdev == bootdev, 1, "so the system variables can be read");
    check(after, before, "coming back leaves it on its own stack as well");
    check(back_again, 0, "and in user mode again");

    printf("# %d checks, %d failed\n", n, fails);
    printf("1..%d\n", n);

    return fails;
}
