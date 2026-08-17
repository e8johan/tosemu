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

#ifndef VDI_P_H
#define VDI_P_H

#include <stdint.h>
#include <stdio.h>

#define VDI_TRACE_CONTEXT
#include "config.h"

/* The parameter block of the call being served.
 *
 * Unlike the AES, the VDI rarely answers with a single value. Results go into
 * the intout and ptsout arrays, and the caller is told how many arrived
 * through the control array, so a handler that produces output says how much
 * of it there is.
 */
int16_t vdi_control(int index);
int16_t vdi_intin(int index);
int16_t vdi_ptsin(int index);
void vdi_set_intout(int index, int16_t value);
void vdi_set_ptsout(int index, int16_t value);

/* How many words the handler put in intout and ptsout. Both start at zero for
 * every call, so a handler with nothing to report says nothing. */
void vdi_set_intout_count(int words);
void vdi_set_ptsout_count(int words);

/* The handle in control[6], naming the workstation the call is for */
int16_t vdi_handle();

/* The sub function in control[5], for the two opcodes that carry one: the
 * generalised drawing primitives and the escapes */
int16_t vdi_subfunction();

#endif /* VDI_P_H */
