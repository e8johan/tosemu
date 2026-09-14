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
 * A program that stays in memory, which is half of a pair: c-usetsr.c is the
 * program that runs on top of it and checks that it is there.
 *
 * This is the shape every TSR of the period had. It installs something the
 * next program can reach - a handler on a spare TRAP vector, which is how
 * Cubase's MROS makes itself callable - leaves a signature where it can be
 * found, and ends with Ptermres rather than Pterm so that the memory it is
 * standing in is not handed to anybody else.
 *
 * TRAP #9 because nothing else wants it. tosemu intercepts traps 1, 2, 13 and
 * 14 for the OS calls and leaves the rest to go through the vector table, so a
 * handler installed here is reached the way one would be on the machine.
 */

#include <stdio.h>
#include <mint/osbind.h>
#include <mint/basepage.h>

#define TRAP_9_VECTOR (0x0A4L)

/*
 * What the next program looks for, and where.
 *
 * The signature sits immediately before the handler, which is the idiom MROS
 * uses: read the vector, look at the four bytes in front of what it points at,
 * and you know both that somebody is there and who it is. It means the next
 * program needs no agreed address to look at - the vector is the only thing it
 * has to know.
 */
#define SIGNATURE (0x54535221L)     /* "TSR!" */

/* And what the handler answers, so that calling it proves it is really being
 * called rather than that the vector merely holds a plausible address */
#define ANSWER (0x1234L)

/*
 * The signature and the handler, together and in that order.
 *
 * Written as one assembly block at file scope so that nothing the compiler
 * does can put anything between them, and so that the handler ends in RTE
 * rather than the RTS an ordinary function would end in. A C function with an
 * RTE in the middle returns through a stack frame the compiler pushed and the
 * RTE knows nothing about.
 */
__asm__(
"       .even                   \n"
"       .long   0x54535221      \n"   /* the signature, just before the entry */
"       .globl  _tsr_handler    \n"   /* the assembler's name for it, which
                                            * this toolchain spells with an
                                            * underscore in front */
"_tsr_handler:                  \n"
"       move.l  #0x1234,%d0     \n"
"       rte                     \n");

extern void tsr_handler(void);

/* The vector table is out of reach of a program in user mode, so installing is
 * done the way every TSR did it */
static void install(void)
{
    *(volatile long *)TRAP_9_VECTOR = (long)tsr_handler;
}

/*
 * How much of this program to keep: the basepage, the three segments, and room
 * for the stack it was standing on. Read out of the basepage rather than
 * guessed at, which is what TOS filled it in for.
 */
static long keep_size(void)
{
    return 0x100L + _base->p_tlen + _base->p_dlen + _base->p_blen + 0x400L;
}

/*
 * Something for Ptermres to have to let go of.
 *
 * A resident keeps what it asked for and releases the rest, and the rest is
 * never nothing: a program that printed anything has a buffer the C library
 * asked for, somewhere above it. This makes that visible - a megabyte is large
 * enough that the program which runs next lands somewhere obviously wrong if
 * it is not released, and the check in c-usetsr.c is exactly that.
 */
#define STRAND (1024L * 1024L)

int main(int argc, char **argv)
{
    long keep = keep_size();
    long spare = (long)Malloc(STRAND);

    Supexec(install);

    printf("# and holding a megabyte at 0x%lx that Ptermres has to release\n",
           spare);

    printf("# a resident program, keeping %ld bytes from 0x%lx\n",
           keep, (long)_base);
    fflush(stdout);

    Ptermres(keep, 0);

    /*
     * Ptermres does not come back. Reaching this at all is the failure, and it
     * is worth saying in the suite's own words rather than merely returning:
     * what runs next is another program, and a count line from here would be
     * mistaken for its.
     */
    printf("not ok 1 - Ptermres came back\n");
    printf("1..1\n");
    fflush(stdout);

    return 1;
}
