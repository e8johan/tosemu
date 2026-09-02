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

#ifndef GEMDOSPROC_P_H
#define GEMDOSPROC_P_H

#include <stdint.h>

/* GEMDOS functions */

uint32_t GEMDOS_Pexec();
uint32_t GEMDOS_Pterm();
uint32_t GEMDOS_Pterm0();
uint32_t GEMDOS_Pgetpid();
uint32_t GEMDOS_Pgetppid();
uint32_t GEMDOS_Pwait();
uint32_t GEMDOS_Pwait3();
uint32_t GEMDOS_Pwaitpid();

#endif /* GEMDOSPROC_P_H */
