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

#ifndef GEMDOSCON_H
#define GEMDOSCON_H

#include <stdint.h>

uint32_t GEMDOS_Cconin();
uint32_t GEMDOS_Cconout();
uint32_t GEMDOS_Cconis();
uint32_t GEMDOS_Cconos();
uint32_t GEMDOS_Cconws();
uint32_t GEMDOS_Cnecin();
uint32_t GEMDOS_Cconrs();
uint32_t GEMDOS_Crawio();
uint32_t GEMDOS_Crawcin();

#endif /* GEMDOSCON_H */
