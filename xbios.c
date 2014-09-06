/*
 * TOSEMU - and emulated environment for TOS applications
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

#include "xbios.h"

#include <stdio.h>
#include <stdlib.h>

#include "tossystem.h"
#include "m68k.h"
#include "utils.h"

/* XBIOS function table according to
 * http://www.yardley.cc/atari/compendium/atari-compendium-XBIOS-Function-Reference.htm
 */
/* TODO fill this out */
#define XBIOS_Getrez (0x04)

void xbios_trap()
{
    uint32_t sp = m68k_get_reg(0, M68K_REG_A7);
    uint16_t fnct;
    
    fnct = endianize_16(m68k_read_disassembler_16(sp));
    
    switch(fnct)
    {
    case XBIOS_Getrez:
        /* Custom value, to ensure that HW-dependent code fails */
        m68k_set_reg(M68K_REG_D0, 8);
        break;

#if 0
        halt_execution();
        printf("GEMDOS unimplemented 0x%x\n", fnct);
        break;
#endif
    default:
        halt_execution();
        printf("XBIOS: Unknown function called 0x%x\n", fnct);
        break;
    }
}
