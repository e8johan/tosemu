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

#ifndef EMUVDI_H
#define EMUVDI_H

#include <stdint.h>

/*
 * What the rest of tosemu is allowed to know about the ported VDI.
 *
 * Everything on the other side of this header is built with its own compiler
 * flags against EmuTOS's headers, where a WORD is a type of its own and the
 * VDI's state is a hundred globals. None of that belongs in tosemu proper, so
 * this is the whole of the surface between them.
 */

/* Readies the VDI: the system fonts and the tables an open workstation
 * reports. Call once, before anything else here. */
void emuvdi_init();

/*
 * Points the VDI at a surface, which is where everything drawn from now on
 * goes. The bitmap is Atari planar - one word of each plane in turn, the
 * highest bit of a word the leftmost pixel - in host byte order.
 */
void emuvdi_surface_select(void *base, uint16_t width, uint16_t height,
                           uint16_t planes);

/*
 * Serves one VDI call.
 *
 * The five arrays are the ones the caller passed, already copied out of the
 * emulated memory, and the answers are written back into them: how many
 * entries of intout and ptsout were filled in goes into control[4] and
 * control[2], and a workstation handle into control[6].
 */
void emuvdi_call(int16_t *control, int16_t *intin, int16_t *ptsin,
                 int16_t *intout, int16_t *ptsout);

/*
 * Whether the VDI has a function for this opcode. Everything it does not is
 * either a call no driver ever implemented or one of the GDOS extensions,
 * which arrive on their own range well above the VDI proper.
 */
int emuvdi_implements(int16_t opcode);

#endif /* EMUVDI_H */
