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

/* Host replacement for EmuTOS's include/xbiosbind.h.
 *
 * The VDI reaches the XBIOS only to set and read palette registers. Those
 * become calls into the surface's palette rather than into video hardware.
 */
#ifndef XBIOSBIND_H
#define XBIOSBIND_H
WORD Setcolor(WORD colornum, WORD color);
WORD EsetColor(WORD colornum, WORD color);
void VsetRGB(WORD index, WORD count, LONG rgb);
void VgetRGB(WORD index, WORD count, LONG rgb);
#endif
