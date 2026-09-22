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
 * How Musashi is built for tosemu.
 *
 * m68k.h reads this in place of its own m68kconf.h because the Makefile names
 * it in MUSASHI_CNF. Only what tosemu decides is here: the submodule's own
 * m68kconf.h is included at the end for everything else, and it defines each
 * option only when nothing before it has.
 */

#ifndef MUSASHICONF_H
#define MUSASHICONF_H

/* An ST has a 68000 and nothing here asks for anything else. Off, the tests
 * for the later processors are constant and compiled away, the FPU's among
 * them - see the SoftFloat stubs in musashi.c. */
#define M68K_EMULATE_010            M68K_OPT_OFF
#define M68K_EMULATE_EC020          M68K_OPT_OFF
#define M68K_EMULATE_020            M68K_OPT_OFF
#define M68K_EMULATE_030            M68K_OPT_OFF
#define M68K_EMULATE_040            M68K_OPT_OFF
#define M68K_EMULATE_PMMU           M68K_OPT_OFF

/*
 * The MFP decides which of its sixteen channels won and therefore which vector
 * the processor goes through, so the acknowledge has to be answered rather
 * than autovectored: an autovector would send every one of them to 0x78, where
 * an ST sends them to 0x100 and up.
 *
 * Turning this on also turns off Musashi's own clearing of the interrupt line -
 * see the #if at the end of m68ki_exception_interrupt - so tos_int_ack has to
 * lower it itself, or the same interrupt is taken again the instant the
 * handler returns, for ever.
 */
int tos_int_ack(int level); /* from interrupt.c */

#define M68K_EMULATE_INT_ACK        M68K_OPT_SPECIFY_HANDLER
#define M68K_INT_ACK_CALLBACK(A)    tos_int_ack(A)

/* Before every instruction. Musashi passes the program counter, which
 * the callback reads for itself when it wants it. */
void cpu_instr_callback(void); /* from main.c */

#define M68K_INSTRUCTION_HOOK       M68K_OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) cpu_instr_callback()

/* Every TRAP #n, before the exception is taken. The OS traps are answered on
 * the host - see musashi.c. */
int musashi_trap(int trap); /* from musashi.c */

#define M68K_TRAP_HAS_CALLBACK      M68K_OPT_SPECIFY_HANDLER
#define M68K_TRAP_CALLBACK(trap)    musashi_trap(trap)

#include "m68kconf.h"

#endif /* MUSASHICONF_H */
