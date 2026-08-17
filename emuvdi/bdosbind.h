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

/* Host replacement for EmuTOS's include/bdosbind.h.
 *
 * The VDI reaches GEMDOS in one place: v_opnvwk allocates the structure
 * describing a virtual workstation, and v_clsvwk gives it back. That memory
 * belongs to the VDI rather than to the application, which never sees it and
 * cannot reach it, so it comes from the host heap and not from the emulated
 * machine's.
 *
 * These are deliberately not the shapes EmuTOS declares. Mxalloc there answers
 * with a LONG, which is thirty two bits wide and cannot hold a pointer on a
 * sixty four bit host, and the caller casts that answer straight to a Vwk *.
 * Returning a pointer is what makes the cast at the call site mean what it
 * says.
 */

#ifndef BDOSBIND_H
#define BDOSBIND_H

/* The mode argument, which asks for supervisor memory. There is one kind of
 * memory here, so it is accepted and ignored. */
#define MX_SUPER (3)

void *host_vdi_alloc(long size);
void host_vdi_free(void *block);

#define Mxalloc(amount, mode) host_vdi_alloc((long)(amount))
#define Mfree(block)          host_vdi_free(block)

#endif /* BDOSBIND_H */
