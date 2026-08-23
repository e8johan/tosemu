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
 * Asking for memory and saying which kind, which is what Mxalloc is for.
 *
 * The TT and the Falcon had a second sort of memory that the chips could not
 * reach, so a program that hands an address to the hardware has to be able to
 * say that this block in particular must not come from there. That is the
 * whole of the difference between this call and Malloc, and it is the reason
 * every compiler's startup and every large application of the period uses it:
 * a machine that does not answer it is a machine they stop on.
 *
 * The emulated machine has one kind of memory, so asking for the ordinary kind
 * and asking for either kind come to the same thing. Only a request for the
 * other kind and nothing else has to fail, because there is none, and handing
 * back the ordinary kind would be giving the caller the memory it had just
 * said it did not want.
 *
 * A call that is not implemented at all stops the emulator, and a test that
 * stops prints nothing further, so the count at the end is what says the whole
 * file ran.
 */

#include <stdio.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>

/* The bits above the two that say which memory are MiNT's memory protection,
 * which needs a memory management unit to mean anything. A machine without one
 * ignores them rather than refusing the call. */
#define A_PROTECTION (MX_GLOBAL)

#define A_BLOCK (4096L)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

static void check_true(long got, const char *name)
{
    n++;
    if (got)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got 0)\n", n, name);
}

int main(int argc, char **argv)
{
    long biggest, block, second, after;

    /* How much there is, asked both ways. A C program has already handed back
     * everything it does not need, so there is something to be had. */
    biggest = Mxalloc(-1L, MX_STRAM);
    check_true(biggest > 0, "there is memory to be had");
    check(biggest, Malloc(-1L),
          "Mxalloc -1 says the same as Malloc -1 about the ordinary memory");
    check(Mxalloc(-1L, MX_PREFSTRAM), biggest,
          "and so does asking for either kind, preferring the ordinary");
    check(Mxalloc(-1L, MX_PREFTTRAM), biggest,
          "and preferring the other kind, there being none to prefer");

    /* The one answer that is not the same, because this machine has no
     * alternative memory and saying so is the whole point of the mode */
    check(Mxalloc(-1L, MX_TTRAM), 0,
          "there is no alternative memory to be had");
    check(Mxalloc(A_BLOCK, MX_TTRAM), 0,
          "so asking for a block of it gets nothing");

    /* An ordinary allocation */
    block = Mxalloc(A_BLOCK, MX_STRAM);
    check_true(block != 0, "a block of the ordinary memory");
    check_true(block >= 0x900L, "which is in the machine's memory");
    check(Mfree((void *)block), 0, "and goes back");

    /*
     * The whole of the largest block there is, which is what a program that
     * wants all it can get asks for and then Mshrinks. It has to be there to
     * be had, and it cannot be there twice: what -1 reports and what the next
     * call hands out are two answers about the same memory, and an emulator
     * that reserves nothing gives the same address to both callers.
     *
     * How much there is is asked again first, because the C library takes some
     * of it for the buffer the checks above were printed through.
     */
    biggest = Mxalloc(-1L, MX_STRAM);
    block = Mxalloc(biggest, MX_STRAM);
    check_true(block != 0, "the largest block there is can be had whole");
    check(Mxalloc(biggest, MX_STRAM), 0, "and there is not a second like it");
    after = Mxalloc(-1L, MX_STRAM);
    check_true(after < biggest, "with less than there was left over");
    check(Mfree((void *)block), 0, "and it goes back");
    check(Mxalloc(-1L, MX_STRAM), biggest, "leaving what there was before");

    /* Two blocks are two blocks. A block handed out twice is the failure this
     * catches, and it is not one an application would survive. */
    block = Mxalloc(A_BLOCK, MX_STRAM);
    check_true(block != 0, "a block");
    second = Mxalloc(A_BLOCK, MX_STRAM);
    check_true(second != 0, "a second block");
    check_true(second >= block + A_BLOCK || block >= second + A_BLOCK,
               "which does not overlap the first");

    /* And it is memory, rather than an address that only looks like one */
    *(long *)block = 0x5AC0FFEEL;
    *(long *)second = 0x0DDBA11L;
    check(*(long *)block, 0x5AC0FFEEL, "what was written into one is there");
    check(*(long *)second, 0x0DDBA11L, "and what was written into the other");

    check(Mfree((void *)second), 0, "a block goes back");
    check(Mfree((void *)block), 0, "and so does the other");
    check(Mxalloc(-1L, MX_STRAM), biggest,
          "and then there is as much to be had as there was");

    /* Either kind, which is what a program that does not care asks for */
    block = Mxalloc(A_BLOCK, MX_PREFSTRAM);
    check_true(block != 0, "a block of whichever kind, preferring the ordinary");
    check(Mfree((void *)block), 0, "goes back");

    block = Mxalloc(A_BLOCK, MX_PREFTTRAM);
    check_true(block != 0,
               "and preferring the other kind falls back to the one there is");
    check(Mfree((void *)block), 0, "and goes back too");

    /* The protection bits, which are not part of which memory is wanted */
    block = Mxalloc(A_BLOCK, MX_STRAM | A_PROTECTION);
    check_true(block != 0, "the memory protection bits are not a refusal");
    check(Mfree((void *)block), 0, "and what they got goes back");

    /* Nothing is not a block of nothing. An address handed out for a block of
     * no bytes is one the caller may write to and one Mfree has to take back,
     * neither of which is true of it. */
    check(Mxalloc(0L, MX_STRAM), 0, "a block of nothing is not an address");

    printf("1..%d\n", n);

    return 0;
}
