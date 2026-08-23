/*
 * TOSEMU - an emulated environment for TOS applications
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

#ifndef GEMDOSMEM_H
#define GEMDOSMEM_H

#include "tossystem.h"

#include <stdint.h>

/* GEMDOS functions */

void gemdos_mem_init(struct tos_environment *);
void gemdos_mem_free();

uint32_t GEMDOS_Mshrink();
uint32_t GEMDOS_Malloc();
uint32_t GEMDOS_Mxalloc();
uint32_t GEMDOS_Mfree();

/* The allocator behind them, for the parts of GEMDOS that have to make room
 * for something without going through a trap. Pexec loads a program into
 * memory it takes from here.
 *
 * mem_alloc returns the base of the block, or 0 when there is no room for it.
 */
uint32_t mem_largest_free(void);
uint32_t mem_alloc(uint32_t size);
int32_t mem_free(uint32_t block);

#endif /* GEMDOSMEM_H */
