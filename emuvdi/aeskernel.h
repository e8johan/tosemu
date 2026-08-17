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

#ifndef AESKERNEL_H
#define AESKERNEL_H

/*
 * The seam between EmuTOS's AES library files and the AES kernel.
 *
 * The library files - the object, form, resource, graphics and file selector
 * ones - are taken from EmuTOS as they stand, because all they do is call the
 * VDI. The kernel underneath them is not: EmuTOS's is cooperative
 * multitasking with every application in one address space, and tosemu runs
 * an application to a process, so appl_*, evnt_*, wind_*, menu_* and shel_*
 * are written here instead.
 *
 * This is the list of everything the library files reach down for, arrived at
 * by compiling them and seeing what was left over. Anything not here they do
 * not need, and anything here that the kernel stops providing will fail to
 * link rather than quietly misbehave.
 *
 * Nothing in it is implemented yet. Until it is, the definitions in
 * aeskernel.c say so when they are reached.
 */

/* Calling a G_USERDEF object's draw routine, which is 68000 code in the
 * emulated machine rather than anything this side can jump to.
 *
 * Include obdefs.h before this: USERBLK and PARMBLK are its, and they are
 * typedefs rather than tagged structures, so there is no way to name them
 * ahead of it.
 */
WORD host_call_userdef(USERBLK *ub, PARMBLK *pb);

#endif /* AESKERNEL_H */
