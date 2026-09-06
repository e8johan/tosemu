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

#ifndef LINEA_H
#define LINEA_H

#include <stdint.h>

/*
 * Lays out the line-A variables in the machine's memory and fills in what the
 * screen decides. Called once the machine is built, since the block is written
 * through the emulated memory and the screen has to be settled first.
 *
 * The sizes are the ones the memory map was built from, so that what a program
 * reads out of the block describes the screen it was actually given.
 */
void linea_init(int16_t width, int16_t height, int16_t planes);

/*
 * Where the line-A variables begin, as the machine sees it, or zero before
 * linea_init has run. This is the address $a000 answers with.
 */
uint32_t linea_vars(void);

#endif /* LINEA_H */
