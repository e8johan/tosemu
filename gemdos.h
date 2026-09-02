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

#ifndef GEMDOS_H
#define GEMDOS_H

#include "tossystem.h"

/* GEMDOS functions */

void gemdos_init(struct tos_environment *);

/* Prepares GEMDOS for an application replacing the one that was running.
 * What belongs to the process rather than to the application - the file
 * handles, the drive table - is left as the new application inherits it. */
void gemdos_reinit(struct tos_environment *);

void gemdos_free();
void gemdos_trap();

#endif /* GEMDOS_H */

/* Starts a program alongside this one, the way the AES's shel_write does. The
 * two addresses are in the machine: a path, and a command line. */
uint32_t tos_start_program(uint32_t prog, uint32_t tail);
