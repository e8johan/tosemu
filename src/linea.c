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
 * The line-A, which is the other way into the graphics.
 *
 * It is not a trap. The opcodes from $a000 to $a00f are not instructions a
 * 68000 has, so executing one raises an exception, and TOS hung its own
 * routines on that vector: a program draws a line by putting its ends in a
 * table and executing $a003. It was the fast way in, and the whole of the
 * interface is one call - $a000, which hands back where that table is.
 *
 * Which is why this exists at all, rather than because anything here draws.
 * Programs that never touch the line-A for drawing still call $a000, because a
 * startup that wants to know how many planes the screen has reads it out of
 * the table rather than opening a workstation to ask. Microsoft Write's loader
 * is one: it calls $a000, stores the two addresses it gets back, and goes on
 * to be an ordinary GEM application that never uses either. Before this, that
 * one instruction ended the run - the vector was nought like every other one,
 * so the processor jumped to address nought and walked through the whole of
 * memory executing the zeroes it found there.
 *
 * So $a000 is answered and the fifteen drawing routines are refused, in the
 * way an unimplemented GEMDOS call is refused: with the emulator stopping and
 * saying which one it was. That is the honest state of it - a program that
 * really draws through the line-A does not work here - and it is worth far
 * more than a routine that quietly does nothing, because what it leaves is a
 * message naming the call instead of a screen with nothing on it.
 */

#include "linea_p.h"
#include "linea.h"

#include <stdio.h>

#include "tossystem.h"
#include "cpu.h"
#include "m68k.h"

/* What the calls are called, for saying which one was asked for. The numbers
 * and the order are the table in 3rdparty/emutos/bios/linea.S. */
static const char *linea_names[LINEA_CALLS] = {
    "init",             "put pixel",        "get pixel",    "line",
    "horizontal line",  "filled rectangle", "polygon line", "blit",
    "text blit",        "show mouse",       "hide mouse",   "transform mouse",
    "undraw sprite",    "draw sprite",      "copy raster",  "flood fill"
};

/* The three things $a000 answers with, as the machine sees them */
static uint32_t vars;
static uint32_t fonts;
static uint32_t routines;

static void write_word(uint32_t address, int16_t value)
{
    m68k_write_memory_16(address, (uint16_t)value);
}

uint32_t linea_vars(void)
{
    return vars;
}

void linea_init(int16_t width, int16_t height, int16_t planes)
{
    uint32_t block;
    uint32_t stub;
    int16_t bytes_per_line;
    int i;

    /*
     * One block with the base in the middle of it, since the variables run
     * both ways from there, and the base is what an application is handed.
     */
    block = bios_static_alloc(LINEA_BELOW + LINEA_ABOVE);
    if (block == 0)
        return;

    vars = block + LINEA_BELOW;

    /*
     * How wide a line of the screen is, worked out the way EmuTOS works it out
     * in update_rez_dependent: the division is by eight and it rounds down.
     * Every Atari screen was a whole number of words across so the rounding
     * never showed, and screen_from_display keeps it that way here.
     */
    bytes_per_line = width / 8 * planes;

    write_word(vars + LINEA_V_REZ_HZ, width);
    write_word(vars + LINEA_V_REZ_VT, height);
    write_word(vars + LINEA_BYTES_LIN, bytes_per_line);
    write_word(vars + LINEA_V_PLANES, planes);
    write_word(vars + LINEA_V_LIN_WR, bytes_per_line);

    /*
     * The parameter block the drawing routines read their arguments out of.
     * Nothing here reads it, every routine that would having been refused
     * above - it is filled in so that a program that writes its arguments
     * there first reaches the refusal rather than writing them through five
     * null pointers, which in this memory map lands on the exception vectors.
     * The lengths are the ceilings vdi.c documents for the same five arrays.
     */
    m68k_write_memory_32(vars + LINEA_CONTRL, bios_static_alloc(15 * 2));
    m68k_write_memory_32(vars + LINEA_INTIN, bios_static_alloc(1024 * 2));
    m68k_write_memory_32(vars + LINEA_PTSIN, bios_static_alloc(1024 * 2));
    m68k_write_memory_32(vars + LINEA_INTOUT, bios_static_alloc(512 * 2));
    m68k_write_memory_32(vars + LINEA_PTSOUT, bios_static_alloc(256 * 4));

    /*
     * The system fonts, which are not here. TOS answered $a000 with the three
     * bitmap fonts the machine had, for a program that draws its own text; the
     * ones tosemu uses are the host's copies and are at host addresses, which
     * a 68000 has no way to reach. The table is present and says there are
     * none - it is read by walking it until a nought - so a program looking
     * for a font finds no fonts rather than finding a wrong one. See TODO.
     */
    fonts = bios_static_alloc(4 * 4);

    /*
     * And the addresses of the routines themselves, for a program that calls
     * one directly rather than by executing its opcode. Each entry points at
     * that opcode followed by an rts, so the two ways in arrive at the same
     * place - including the refusal, which is the whole reason not to leave
     * the table empty: an entry of nought is a jump to address nought, which
     * is the failure this file exists to stop.
     */
    routines = bios_static_alloc(LINEA_CALLS * 4);

    for (i = 0; i < LINEA_CALLS; i++)
    {
        stub = bios_static_alloc(4);

        m68k_write_memory_16(stub, 0xa000 | i);
        m68k_write_memory_16(stub + 2, 0x4e75); /* rts */

        m68k_write_memory_32(routines + i * 4, stub);
    }
}

/*
 * The line-A exception, called from Musashi in place of taking it.
 *
 * Returns whether it was dealt with. Anything that is not one of the sixteen
 * is left to the processor, which vectors it the way the hardware would - a
 * program that installed a handler of its own on the vector is entitled to
 * have it called.
 */
int m68k_linea(unsigned int opcode)
{
    unsigned int call = opcode & 0xf;

    if ((opcode & 0xfff0) != 0xa000)
        return 0;

    FUNC_TRACE_ENTER_ARGS {
        printf("    $%04x, line-A %s\n", opcode, linea_names[call]);
    }

    if (call != 0)
    {
        halt_execution();
        printf("line-A %s ($%04x) not implemented\n",
               linea_names[call], opcode);

        return 1;
    }

    /*
     * $a000 answers in registers rather than on the stack: the variables in
     * both d0 and a0, the fonts in a1 and the routine table in a2. See
     * _linea_0 in 3rdparty/emutos/bios/linea.S.
     */
    m68k_set_reg(M68K_REG_D0, vars);
    m68k_set_reg(M68K_REG_A0, vars);
    m68k_set_reg(M68K_REG_A1, fonts);
    m68k_set_reg(M68K_REG_A2, routines);

    return 1;
}
