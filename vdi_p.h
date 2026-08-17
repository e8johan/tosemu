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

/* The handle in control[6], naming the workstation the call is for, and the
 * sub function in control[5], for the two opcodes that carry one: the
 * generalised drawing primitives and the escapes.
 *
 * There are no other accessors, and no per function handlers. A VDI call is
 * unpacked into host arrays and handed to EmuTOS's own dispatcher whole - see
 * emuvdi/emuvdi.h - so nothing here reaches into the arrays a value at a time.
 */
int16_t vdi_handle();
int16_t vdi_subfunction();

#endif /* VDI_P_H */
