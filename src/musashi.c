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
 * Everything that adapts Musashi to tosemu, and the one file here that reaches
 * inside it.
 *
 * Musashi is a submodule, 3rdparty/musashi, and nothing in it is edited. What
 * tosemu needs of it that its configuration cannot say is done from here,
 * through its internal header - the same one the opcode handlers m68kmake
 * writes are compiled against.
 */

#include <stdio.h>
#include <stdlib.h>

#include "musashi.h"
#include "tossystem.h"
#include "linea.h"

#include "m68kcpu.h"
#include "m68kops.h"

/*
 * A TRAP #n, before the processor takes it. Returns whether it was answered.
 *
 * The four OS traps are made on the host without going near their vectors,
 * unless a program has hooked one - then the trap is taken the way the
 * hardware takes it, and reaches the host when the program's handler chains
 * to the one that was there before it. See the line-F handler below.
 */
int musashi_trap(int trap)
{
    unsigned int vector = EXCEPTION_TRAP_BASE + trap;

    switch (trap)
    {
        case 1:  /* GEMDOS */
        case 2:  /* GEM */
        case 13: /* BIOS */
        case 14: /* XBIOS */
            if (!m68k_trap_vectored(vector))
            {
                m68k_trap(vector);
                return 1;
            }
            break;
    }

    return 0;
}

/*
 * Line-A, answered on the host the way the OS traps are. An opcode m68k_linea
 * does not know is vectored the way the hardware would, so that a program
 * with a handler of its own still has it called.
 */
static void linea(void)
{
    if (m68k_linea(REG_IR))
        return;

    m68ki_exception_1010();
}

/*
 * Line-F, which is how a program that hooked an OS trap reaches the handler
 * that was on the vector before it: each of those is a line-F opcode of its
 * own - see where tossystem.c writes them.
 *
 * It is an RTE that makes the call on the way: the frame is taken off first,
 * so the mode and the stack are the caller's again and the arguments are where
 * the host looks for them - on the user stack for a caller in user mode, just
 * past the frame for one in supervisor mode, which is where TOS looks. The
 * return address goes in before the call, so that a call which moves execution
 * elsewhere itself, as Pexec and Pterm do, wins.
 *
 * The mask is put back without looking for interrupts, which is what the trap
 * does when nobody has hooked it: one that a handler held off while it ran is
 * taken the next time the interrupts are looked at rather than in the middle
 * of the call. A 68000's frame, which is the only one this machine makes.
 */
static void linef(void)
{
    uint vector = m68k_trap_stub(ADDRESS_68K(REG_PPC));

    if (vector && FLAG_S)
    {
        uint new_sr = m68ki_pull_16();
        uint new_pc = m68ki_pull_32();

        m68ki_jump(new_pc);
        m68ki_set_sr_noint(new_sr);
        m68k_trap(vector);
        return;
    }

    m68ki_exception_1111();
}

/*
 * Musashi has no callback for either, so they go into its opcode table in
 * place of its own handlers. All of each range, and not only the entries its
 * own line-A and line-F handlers are on: the table gives much of the line-F
 * range to the FPU and the MMU, whose handlers take the same exception on a
 * 68000, and the opcodes the OS trap stubs are written with land there.
 */
void musashi_hook_opcodes(void)
{
    int opcode;

    for (opcode = 0xa000; opcode <= 0xafff; opcode++)
        m68ki_instruction_jump_table[opcode] = linea;

    for (opcode = 0xf000; opcode <= 0xffff; opcode++)
        m68ki_instruction_jump_table[opcode] = linef;
}

/*
 * m68k_set_irq only records the level; Musashi looks at it when m68k_execute
 * is next called and when the status register changes. Looking now is what
 * puts the handler on the instruction that was about to happen anyway, rather
 * than on the one after it.
 */
void musashi_interrupt(int level)
{
    m68k_set_irq(level);
    m68ki_check_interrupts();
}

/* What a MOVE to SR does. m68k_set_reg stopped doing it in 2019, for loading a
 * saved state where the stack pointers are set on their own. */
void musashi_set_sr(unsigned int value)
{
    m68ki_set_sr(value);
}

/*
 * The FPU's arithmetic, which Musashi does with SoftFloat.
 *
 * SoftFloat is in the submodule and is left out of the build: it is release
 * 2b, whose licence the FSF holds to be incompatible with the GPL. m68kcpu.c
 * includes the FPU whatever the configuration says, so the functions it calls
 * are defined here instead. Nothing reaches them - a 68000 has no FPU, and
 * every line-F opcode, which is how one is spoken to, is handled above.
 */
static void no_fpu(const char *function)
{
    fprintf(stderr, "tosemu: %s called, and a 68000 has no FPU\n", function);
    abort();
}

int8 float_rounding_mode;

floatx80 int32_to_floatx80(int32 a)
{
    no_fpu(__func__);
    return (floatx80){ 0 };
}

floatx80 float32_to_floatx80(float32 a)
{
    no_fpu(__func__);
    return (floatx80){ 0 };
}

floatx80 float64_to_floatx80(float64 a)
{
    no_fpu(__func__);
    return (floatx80){ 0 };
}

int32 floatx80_to_int32(floatx80 a)
{
    no_fpu(__func__);
    return 0;
}

int32 floatx80_to_int32_round_to_zero(floatx80 a)
{
    no_fpu(__func__);
    return 0;
}

float32 floatx80_to_float32(floatx80 a)
{
    no_fpu(__func__);
    return 0;
}

float64 floatx80_to_float64(floatx80 a)
{
    no_fpu(__func__);
    return 0;
}

floatx80 floatx80_add(floatx80 a, floatx80 b)
{
    no_fpu(__func__);
    return a;
}

floatx80 floatx80_sub(floatx80 a, floatx80 b)
{
    no_fpu(__func__);
    return a;
}

floatx80 floatx80_mul(floatx80 a, floatx80 b)
{
    no_fpu(__func__);
    return a;
}

floatx80 floatx80_div(floatx80 a, floatx80 b)
{
    no_fpu(__func__);
    return a;
}

floatx80 floatx80_rem(floatx80 a, floatx80 b)
{
    no_fpu(__func__);
    return a;
}

floatx80 floatx80_sqrt(floatx80 a)
{
    no_fpu(__func__);
    return a;
}

flag floatx80_is_nan(floatx80 a)
{
    no_fpu(__func__);
    return 0;
}
