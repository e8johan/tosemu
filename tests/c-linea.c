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

/* The line-A, which is a graphics interface reached by executing an
 * instruction a 68000 does not have rather than by trapping.
 *
 * Only $a000 is answered, and this is what it has to answer with. What the
 * checks are really guarding is that none of the three addresses it hands back
 * is nought: a program follows all of them, and following a nought here means
 * reading or jumping into the exception vectors at the bottom of memory, which
 * is a failure that shows up somewhere else entirely.
 *
 * The rest ask whether the block describes the screen the machine was actually
 * given, by working the screen's size out of it two ways and comparing that
 * against what the XBIOS says. The drawing routines are refused rather than
 * implemented, and the refusal stops the emulator, so it is checked from the
 * Makefile against what the emulator said rather than from in here.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/falcon.h>

/* Where the variables are, as offsets from the address $a000 hands back. Half
 * of them are below it, the base being a pointer into the middle of a block. */
#define V_REZ_HZ    (-12)
#define V_REZ_VT     (-4)
#define BYTES_LIN    (-2)
#define V_PLANES      (0)
#define V_LIN_WR      (2)

/* The sixteen line-A calls, $a000 to $a00f */
#define LINEA_CALLS  (16)

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

/* $a000, which answers in four registers rather than on the stack: the
 * variables in both d0 and a0, the system fonts in a1, and the addresses of
 * the routines in a2 */
static long lv_d0, lv_a0, lv_a1, lv_a2;

static void linea_init(void)
{
    register long d0 __asm__("d0");
    register long a0 __asm__("a0");
    register long a1 __asm__("a1");
    register long a2 __asm__("a2");

    __asm__ volatile (".word 0xa000"
                      : "=r" (d0), "=r" (a0), "=r" (a1), "=r" (a2)
                      :
                      : "cc", "memory");

    lv_d0 = d0;
    lv_a0 = a0;
    lv_a1 = a1;
    lv_a2 = a2;
}

/* Calling one of the routines the way a program that took its address out of
 * the table would, rather than by executing the opcode. Answers with a0, which
 * for the first of them is where the variables are. */
static long call_routine(long addr)
{
    register long a3 __asm__("a3") = addr;
    register long a0 __asm__("a0");

    __asm__ volatile ("jsr %%a3@"
                      : "=r" (a0)
                      : "r" (a3)
                      : "d0", "d1", "a1", "a2", "cc", "memory");

    return a0;
}

static short word_at(long addr)
{
    return *(volatile short *)addr;
}

int main(int argc, char **argv)
{
    long planes, width, height, line;
    int i;

    linea_init();

    check(lv_a0 != 0, 1, "$a000 says where the line-A variables are");
    check(lv_d0 == lv_a0, 1, "in d0 as well as a0");
    check(lv_a1 != 0, 1, "and hands over a table of system fonts");
    check(lv_a2 != 0, 1, "and one of the addresses of the routines");

    planes = word_at(lv_a0 + V_PLANES);
    width = word_at(lv_a0 + V_REZ_HZ);
    height = word_at(lv_a0 + V_REZ_VT);
    line = word_at(lv_a0 + BYTES_LIN);

    check(planes > 0, 1, "the screen it describes has planes");
    check(width > 0 && height > 0, 1, "and a size");

    check(word_at(lv_a0 + V_LIN_WR), line,
          "v_lin_wr and BYTES_LIN are the same line length");
    check(line, width / 8 * planes, "which is what a line of that screen is");

    /* And the same screen the machine was built around, rather than some other
     * one: the block is written when the memory map is, and this is the one
     * question that can tell the two apart */
    check(line * height, VgetSize(0), "and the screen is that many lines of it");

    /* A program that takes an address out of the table and calls it must not
     * be calling address nought, which is where a table nobody filled in would
     * send it - and that is a jump into the exception vectors */
    for (i = 0; i < LINEA_CALLS; i++)
        if (*(volatile long *)(lv_a2 + i * 4) == 0)
            break;

    check(i, LINEA_CALLS, "no entry in the routine table is address nought");

    /* Both ways in reach the same routine */
    check(call_routine(*(volatile long *)lv_a2), lv_a0,
          "calling the first through the table is the same as $a000");

    printf("# %d checks, %d failed\n", n, fails);
    printf("1..%d\n", n);

    /*
     * And a drawing routine, which is refused. It stops the emulator, so it
     * comes after everything has been said and only when the run asked for it:
     * what it leaves behind is on the emulator's own output rather than here.
     */
    if (argc > 1 && strcmp(argv[1], "DRAW") == 0)
        __asm__ volatile (".word 0xa001" : : : "cc", "memory");

    return fails;
}
