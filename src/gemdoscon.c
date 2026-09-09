/*
 * TOSEMU - an emulated environment for TOS applications
 * Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
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

#include "gemdoscon_p.h"

#include <stdio.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "utils.h"
#include "m68k.h"

#include "gemdos_p.h"

/* Console I/O functions *****************************************************/

/*
 * All of these go through console.c, which decides whether the console is a
 * window on the desktop or the terminal the emulator was started from. What is
 * left here is the shape GEMDOS gives each of them - which of them waits,
 * which echoes, and what the answer looks like - because that is the part an
 * application depends on and it is the part that used to be wrong.
 *
 * The three that wait are Cconin, Cnecin and Crawcin. That is what an
 * application expects of them: a program asking for a key with nothing to read
 * is a program that has stopped until somebody presses one. They used to
 * answer nought straight away, which is a key that was never pressed.
 *
 * The one that does not wait is Crawio with 0xff, which is the whole point of
 * that call: it asks whether a key is there and answers nought when none is.
 * A program polling it goes round its loop until one arrives - GenST does
 * exactly this to wait for the keypress that dismisses its assembler output -
 * and the loop only ends because something else eventually puts a key in.
 */

uint32_t GEMDOS_Cconin()
{
    uint32_t key;

    FUNC_TRACE_ENTER

    key = console_key(1);

    /* Cconin echoes what was typed and Cnecin does not, which is the whole
     * difference between the two */
    if (key & 0xff)
        console_out(key & 0xff);

    return key;
}

uint32_t GEMDOS_Cnecin()
{
    FUNC_TRACE_ENTER

    return console_key(1);
}

uint32_t GEMDOS_Cconout()
{
    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x '%c'\n", peek_u16(2), peek_u16(2)&0xff);
    }

    console_out(peek_u16(2)&0xff);
    return 0;
}

uint32_t GEMDOS_Cconis()
{
    FUNC_TRACE_ENTER

    if (console_ready())
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
        console_out(ch);
        res++;
    }

    return res;
}

uint32_t GEMDOS_Cconrs()
{
    char buf[255]; /* Max len on ST side is 255 */

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

    int maxlen = m68k_read_memory_8(lineptr);
    int len;
    int i;

    if (maxlen > (int)sizeof buf)
        maxlen = (int)sizeof buf;

    len = console_line(buf, maxlen);

    m68k_write_memory_8(lineptr+1, len);

    for (i = 0; i < len; i++)
        m68k_write_memory_8(lineptr + 2 + i, (uint8_t)buf[i]);

    return 0;
}

uint32_t GEMDOS_Crawio()
{
    uint32_t w = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", w);
    }

    if (w == 0xff)
        return console_key(0);

    /* Anything else is a character to write */
    console_out(w & 0xff);

    return 0;
}

uint32_t GEMDOS_Crawcin()
{
    /*FUNC_TRACE_ENTER*/

    return console_key(1);
}
