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

#ifndef XBIOS_P_H
#define XBIOS_P_H

#include <stdint.h>
#include <stdio.h>

#define XBIOS_TRACE_CONTEXT
#include "config.h"

/* XBIOS return values, http://toshyp.atari.org/en/004003.html */

#define XBIOS_E_OK   (0)
#define XBIOS_ERROR  (-1) /* Generic error */
#define XBIOS_EDRVNR (-2) /* Drive not ready */

/* Screen functions, xbiosscreen.c */

uint32_t XBIOS_Getrez();
uint32_t XBIOS_Physbase();
uint32_t XBIOS_Logbase();
uint32_t XBIOS_Setscreen();
uint32_t XBIOS_Setpalette();
uint32_t XBIOS_Setcolor();
uint32_t XBIOS_Cursconf();
uint32_t XBIOS_EsetColor();
uint32_t XBIOS_EsetPalette();
uint32_t XBIOS_EgetPalette();
uint32_t XBIOS_VsetMode();
uint32_t XBIOS_VgetSize();
uint32_t XBIOS_VsetRGB();
uint32_t XBIOS_VgetRGB();

/* System functions, xbiossys.c */

uint32_t XBIOS_Supexec();
uint32_t XBIOS_Gettime();
uint32_t XBIOS_Settime();
uint32_t XBIOS_Random();
uint32_t XBIOS_NVMaccess();
uint32_t XBIOS_Dbmsg();
uint32_t XBIOS_Metainit();

/* Device functions, xbiosdev.c */

uint32_t XBIOS_Rsconf();
uint32_t XBIOS_Bconmap();
uint32_t XBIOS_Setprt();
uint32_t XBIOS_Prtblk();
uint32_t XBIOS_Midiws();
uint32_t XBIOS_Ikbdws();
uint32_t XBIOS_Initmous();
uint32_t XBIOS_Iorec();
uint32_t XBIOS_Kbdvbase();
uint32_t XBIOS_Kbrate();
uint32_t XBIOS_Mfpint();
uint32_t XBIOS_Jenabint();
uint32_t XBIOS_Jdisint();
uint32_t XBIOS_Xbtimer();

#endif /* XBIOS_P_H */
