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

#ifndef CPU_H
#define CPU_H

#include <stdint.h>

/* Enable, disable and check supervisor mode */
void enable_supervisor_mode();
void disable_supervisor_mode();
int is_supervisor_mode_enabled();

/* Peek at the stack.
 * Returns host system endian values */
uint8_t peek_u8(int offset);
uint16_t peek_u16(int offset);
uint32_t peek_u32(int offset);

#endif /* GEMDOS_H */
