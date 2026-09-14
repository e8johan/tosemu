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
 * The program that runs on top of one that stayed - the other half of
 * tests/c-tsr.c.
 *
 * Three things have to be true of it and they are separate. The resident
 * program has to still be there, which is the signature. It has to still work,
 * which is calling it - a vector holding a plausible address is not the same
 * as a routine that runs. And the memory it is standing in has to be nobody
 * else's, which is where this program was loaded and what Malloc will hand out.
 *
 * Run twice by the suite: with the resident and without. Without it, every
 * check has to come out the other way rather than the program failing - a
 * program that looks for a TSR and does not find one is the ordinary case, and
 * on an ST it put up a polite alert rather than crashing.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/basepage.h>

#define TRAP_9_VECTOR (0x0A4L)

#define SIGNATURE (0x54535221L)     /* "TSR!" */
#define ANSWER    (0x1234L)

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

/* What the vector holds and what is just in front of it, gathered in
 * supervisor mode and reported afterwards - the vector table cannot be read by
 * a program in user mode, and printf from in there is far more stack than a
 * Supexec'd routine should spend */
static long vector;
static long signature;

static void look(void)
{
    vector = *(volatile long *)TRAP_9_VECTOR;

    /*
     * Four bytes before whatever the vector points at, which is where a TSR of
     * this shape leaves its mark. Only worth reading if the vector points into
     * the machine's own RAM at all: with nothing resident it holds the address
     * the table is filled with, which is up in the memory the system keeps its
     * own structures in, and reading in front of that says nothing.
     */
    if (vector > 0x900L && vector < 0xF00000L)
        signature = *(volatile long *)(vector - 4);
    else
        signature = 0;
}

/* Calling the resident routine, which is a trap like any other */
static long call_the_resident(void)
{
    long answer;

    __asm__ volatile ("trap #9\n\tmove.l %%d0,%0" : "=d" (answer) : : "d0", "cc");

    return answer;
}

int main(int argc, char **argv)
{
    /* "alone" says there is to be no resident, which is how the suite runs
     * this the second time. Anything else, including nothing at all, expects
     * one. */
    int expecting = !(argc > 1 && strcmp(argv[1], "alone") == 0);
    long mine = (long)_base;
    long block;

    Supexec(look);

    if (!expecting)
    {
        /*
         * No resident, which is what every other test in this directory runs
         * as. The signature is not there and the program carries on being a
         * program - which is the whole of what has to be true.
         */
        check(signature == SIGNATURE, 0, "with nothing resident there is no signature");
        check(mine == 0x800L, 1, "and this program has the machine to itself");

        printf("1..%d\n", n);
        return fails;
    }

    check(signature, SIGNATURE, "the resident program is still there");
    check(call_the_resident(), ANSWER, "and still answers when it is called");

    /*
     * And this program was loaded above it rather than on top of it. 0x800 is
     * where the first program in a machine goes, so being anywhere else is
     * being somewhere that was decided by what came before.
     */
    check(mine > 0x800L, 1, "this program was loaded above the resident one");
    check(mine > vector, 1, "which is to say above the code it is calling");

    /*
     * And only just above it. The resident held a megabyte when it finished
     * and asked to keep only itself, so Ptermres had to let that megabyte go -
     * a program loaded above it instead of above what was kept would be one
     * whose machine is a megabyte smaller than it should be, for memory nobody
     * owns and nothing can reach.
     */
    printf("# this program is at 0x%lx, the resident routine at 0x%lx\n",
           mine, vector);
    check(mine < 0x100000L, 1,
          "and only above what the resident kept, not what it merely held");

    /*
     * And the resident's memory is not going to be handed to anybody. Mshrink
     * first, so that there is something to allocate out of - a program is
     * given the whole of what is above it and has to give back what it does not
     * want, which is what the next line does.
     */
    Mshrink(_base, 0x100L + _base->p_tlen + _base->p_dlen + _base->p_blen
                   + 0x400L);

    block = (long)Malloc(0x1000L);
    check(block != 0, 1, "there is still memory to be had");
    check(block > vector, 1, "and none of it is where the resident program is");

    /*
     * And it cannot be taken deliberately either. 0x800 is where the first
     * program in a machine goes, so it is the resident's own block, and a
     * program that freed it would be handing the code it is calling to
     * whatever asks for memory next - while going on calling it.
     */
    check(Mfree((void *)0x800L) != 0, 1,
          "and the resident's own block cannot be freed out from under it");

    printf("1..%d\n", n);

    return fails;
}
