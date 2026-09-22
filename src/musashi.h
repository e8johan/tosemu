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

#ifndef MUSASHI_H
#define MUSASHI_H

/*
 * Puts tosemu's own handlers in for every line-A and line-F opcode. Called
 * after m68k_init, which is what builds the table they go into.
 */
void musashi_hook_opcodes(void);

/*
 * Raises the interrupt line to `level` and takes the interrupt now if the
 * mask lets it through, frame built and program counter on the handler before
 * this returns. Only from between two instructions - the instruction hook.
 */
void musashi_interrupt(int level);

/*
 * Writes the status register the way the processor does: a change of mode
 * swaps a7 for the other stack pointer, and an interrupt the new mask lets
 * through is taken. m68k_set_reg(M68K_REG_SR) does neither - it only files the
 * bits away - which is never what anything here means by it.
 */
void musashi_set_sr(unsigned int value);

#endif /* MUSASHI_H */
