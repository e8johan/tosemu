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
 * $a000 is the one that answers with something, and most of this is about what
 * it has to answer with. What those checks are really guarding is that none of the three addresses it hands back
 * is nought: a program follows all of them, and following a nought here means
 * reading or jumping into the exception vectors at the bottom of memory, which
 * is a failure that shows up somewhere else entirely.
 *
 * Showing and hiding the mouse pointer are answered too, by doing nothing, and
 * what is checked about them is that the program is still running afterwards:
 * a program hides the pointer around drawing it does by other means, and a
 * refusal there would stop it between the hiding and the drawing.
 *
 * The rest ask whether the block describes the screen the machine was actually
 * given, by working the screen's size out of it two ways and comparing that
 * against what the XBIOS says; and whether the system fonts are really there,
 * in the machine's memory and the machine's byte order, since a program that
 * draws its own text reads the letters straight out of them - a debugger with
 * a screen of its own does exactly that. The drawing routines are refused rather than
 * implemented, and the refusal stops the emulator, so it is checked from the
 * Makefile against what the emulator said rather than from in here.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/falcon.h>

/* Where the variables are, as offsets from the address $a000 hands back. Half
 * of them are below it, the base being a pointer into the middle of a block. */
#define V_CEL_HT    (-46)
#define V_CEL_MX    (-44)
#define V_CEL_MY    (-42)
#define V_FNT_AD    (-22)
#define V_FNT_WR    (-14)
#define V_REZ_HZ    (-12)
#define V_OFF_AD    (-10)
#define V_REZ_VT     (-4)
#define BYTES_LIN    (-2)
#define V_PLANES      (0)
#define V_LIN_WR      (2)

/* And a font header's, which is the same in a font file */
#define FONT_FIRST_ADE   (36)
#define FONT_LAST_ADE    (38)
#define FONT_OFF_TABLE   (72)
#define FONT_DAT_TABLE   (76)
#define FONT_FORM_WIDTH  (80)
#define FONT_FORM_HEIGHT (82)
#define FONT_NEXT_FONT   (84)

/* The letter A in the 8x8 and the 8x16 system fonts, a row at a time, which
 * is what fnt_st_8x8.c and fnt_st_8x16.c in EmuTOS have. The 8x8's is the
 * one that shows the byte order: A is an odd character, so the byte beside it
 * in the same word is the @ before it. */
static const unsigned char a_8x8[8] = {
    0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x00
};
static const unsigned char a_8x16[16] = {
    0x00, 0x00, 0x18, 0x3c, 0x7e, 0x66, 0x66, 0x66,
    0x7e, 0x7e, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00
};

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

static long long_at(long addr)
{
    return *(volatile long *)addr;
}

/* Whether the letter A reads out of this font as it should */
static int has_a(long font, const unsigned char *rows, int height)
{
    long raster = long_at(font + FONT_DAT_TABLE);
    long across = word_at(font + FONT_FORM_WIDTH);
    int row;

    for (row = 0; row < height; row++)
        if (*(volatile unsigned char *)(raster + row * across + 'A')
            != rows[row])
            return 0;

    return 1;
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

    /* The system fonts: the 6x6, the 8x8 and the 8x16, and then a nought */
    {
        long f6 = long_at(lv_a1), f8 = long_at(lv_a1 + 4),
             f16 = long_at(lv_a1 + 8);
        long console = height < 400 ? f8 : f16;

        check(f6 != 0 && f8 != 0 && f16 != 0, 1,
              "the font table lists three fonts");
        check(long_at(lv_a1 + 12), 0, "and ends there");
        check(word_at(f6 + FONT_FORM_HEIGHT), 6, "the first is six lines tall");
        check(word_at(f8 + FONT_FORM_HEIGHT), 8, "the second eight");
        check(word_at(f16 + FONT_FORM_HEIGHT), 16, "and the third sixteen");
        check(word_at(f8 + FONT_FIRST_ADE) == 0
              && word_at(f8 + FONT_LAST_ADE) == 255, 1,
              "each has the whole character set");
        check(long_at(f8 + FONT_NEXT_FONT), f16,
              "the 8x8 leads on to the 8x16, the way TOS chains them");
        check(has_a(f8, a_8x8, 8), 1,
              "the 8x8's letters are in the machine's byte order");
        check(has_a(f16, a_8x16, 16), 1, "and so are the 8x16's");

        /* And which of them the console writes in, which the variables say */
        check(long_at(lv_a0 + V_FNT_AD), long_at(console + FONT_DAT_TABLE),
              "the console's font is the one that fits this screen");
        check(long_at(lv_a0 + V_OFF_AD), long_at(console + FONT_OFF_TABLE),
              "and its offsets are that font's");
        check(word_at(lv_a0 + V_FNT_WR), word_at(console + FONT_FORM_WIDTH),
              "and so is how wide its raster is");
        check(word_at(lv_a0 + V_CEL_HT), height < 400 ? 8 : 16,
              "a character cell is that font's height");
        check(word_at(lv_a0 + V_CEL_MX), width / 8 - 1,
              "and there are as many across as eight pixels fit");
        check(word_at(lv_a0 + V_CEL_MY), height / (height < 400 ? 8 : 16) - 1,
              "and as many down as the cell fits");
    }

    /*
     * Showing and hiding the pointer, which are the two that do nothing and
     * come back. Reaching the check after them is the whole of what is asked:
     * a call that is refused stops the emulator, so a line-A that does not
     * answer these prints nothing more and has no count line at the end.
     */
    __asm__ volatile (".word 0xa009"
                      : : : "d0", "d1", "a0", "a1", "a2", "cc", "memory");
    __asm__ volatile (".word 0xa00a"
                      : : : "d0", "d1", "a0", "a1", "a2", "cc", "memory");

    check(1, 1, "showing and hiding the mouse pointer carries on");

    /* And by their addresses, which is the other way a program reaches them */
    call_routine(*(volatile long *)(lv_a2 + 9 * 4));
    call_routine(*(volatile long *)(lv_a2 + 10 * 4));

    check(word_at(lv_a0 + V_PLANES), planes,
          "and through the routine table, leaving the variables alone");

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
