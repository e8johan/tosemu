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

/* GEMDOS return values */

#define E_OK (0)
#define EINVFN (32)
#define EFILNF (33)
#define EPTHNF (-34)
#define ENHNDL (-35)
#define EACCDN (-36)
#define EIHNDL (-37)
#define ENSMEM (-39)
#define EIMBA (-40)
#define EDRIVE (-46)
#define ECWD (-47)
#define ENSAME (-48)
#define ENMFIL (-49)
#define ELOCKED (-58)
#define ENSLOCK (-59)
#define ERANGE (-64)
#define EINTRN (-65)
#define EPLFMT (-66)
#define EGSBF (-67)
#define EBREAK (-68)
#define EXCPT (-69)
#define EPTHOV (-70)
#define ELOOP (-80)
#define EPIPE (-81)

/* GEMDOS trap function */

void gemdos_init();
void gemdos_trap();

#endif /* GEMDOS_H */
