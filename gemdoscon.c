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

#include "gemdoscon_p.h"

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "utils.h"
#include "m68k.h"

#include "gemdos_p.h"

/* Console I/O functions *****************************************************/

uint32_t GEMDOS_Cconin()
{   
    FUNC_TRACE_ENTER
    
    return getchar() & 0xff; /* TODO no shift key status, scancode */
}

uint32_t GEMDOS_Cnecin()
{   
    FUNC_TRACE_ENTER
    
    /* TODO: turn off not echo. */
    return getchar() & 0xff; /* TODO no shift key status, scancode */
}

uint32_t GEMDOS_Cconout()
{
    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x '%c'\n", peek_u16(2), peek_u16(2)&0xff);
    }

    putchar(peek_u16(2)&0xff);
    return 0;
}

uint32_t GEMDOS_Cconis()
{
    FUNC_TRACE_ENTER

    if (console_input_available())
        return -1;
    else
        return 0;
}

uint32_t GEMDOS_Cconos()
{
    FUNC_TRACE_ENTER

    return -1; /* Always ready */
}

uint32_t GEMDOS_Cconws()
{
    uint32_t adr = peek_u32(2);
    uint32_t res = 0;
    uint8_t ch;

    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", adr);
    }
    
    while((ch=m68k_read_disassembler_8(adr++)))
    {
        putchar(ch);
        res++;
    }
    
    return res;
}

uint32_t GEMDOS_Cconrs()
{
    char buf[257]; /* Max len on ST side is 256 */

    /* This is a pointer to a LINE struct, i.e.
     *
     * typedef struct
     * {
     *   uint8_t   maxlen;        * Maximum line length *
     *   uint8_t   actuallen;     * Current line length *
     *   int8_t    buffer[255];   * Line buffer         *
     * } LINE;
     */
    uint32_t lineptr = peek_u32(2);

    uint8_t maxlen = m68k_read_memory_8(lineptr);

    fgets(buf, maxlen, stdin);
    int len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n')
    {
        /* Remove final \n */
        buf[len] = '\0';
        len --;
    }

    if (len > maxlen)
    {
        /* Truncate buffer string if necessary */
        len = maxlen;
        buf[len] = '\0';
    }

    m68k_write_memory_8(lineptr+1, len);
    char *ptr = buf;
    lineptr += 2;
    do
    {
        m68k_write_memory_8(lineptr, *ptr);
        ptr ++;
        lineptr ++;
    } while (*ptr != '\0');

    return 0;
}
