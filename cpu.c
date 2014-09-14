/*
 * TOSEMU - and emulated environment for TOS applications
 * Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
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

#include "cpu.h"

#include "utils.h"
#include "m68k.h"

void enable_supervisor_mode()
{
    m68k_set_reg(M68K_REG_SR, m68k_get_reg(0, M68K_REG_SR) | 0x2000); /* set the CPU in supervisor mode */
}

void disable_supervisor_mode()
{
    m68k_set_reg(M68K_REG_SR, m68k_get_reg(0, M68K_REG_SR) & (~0x2000)); /* set the CPU in supervisor mode */
}

int is_supervisor_mode_enabled()
{
    if ((m68k_get_reg(0, M68K_REG_SR) & 0x2000) == 0x2000) {
        return 1;
    } else {
        return 0;
    }
}

void push_u8(uint8_t value);
void push_u16(uint16_t value);
void push_u32(uint32_t value);

uint8_t pop_u8();
uint16_t pop_u16();
uint32_t pop_u32();

uint8_t peek_u8(int offset)
{
    uint32_t sp = m68k_get_reg(0, M68K_REG_A7);
    return m68k_read_disassembler_8(sp+offset);
}

uint16_t peek_u16(int offset)
{
    uint32_t sp = m68k_get_reg(0, M68K_REG_A7);
    return endianize_16(m68k_read_disassembler_16(sp+offset));
}

uint32_t peek_u32(int offset)
{
    uint32_t sp = m68k_get_reg(0, M68K_REG_A7);
    return endianize_32(m68k_read_disassembler_32(sp+offset));
}
