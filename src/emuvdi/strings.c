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

/*
 * Two string routines EmuTOS keeps in assembly, in C.
 *
 * util/optimopt.S and util/stringasm.S, which are assembly to be quick on a
 * 68000 rather than because they do anything a compiler cannot. Both are
 * reached from the AES library files, and both have a return value that is not
 * the obvious one, so they are worth writing out rather than reaching for the
 * nearest standard function.
 */

#include "emutos.h"
#include "asm.h"

/*
 * Widens a byte string into an array of words, one character to a word, and
 * answers how long the string was.
 *
 * This is how a string reaches the VDI: v_gtext takes its characters in intin,
 * which is words. The terminating zero is copied as well - the original does
 * it to save a test in the loop - but is not counted.
 */
WORD expand_string(WORD *dest, const char *src)
{
    WORD length = 0;

    while ((*dest++ = (UBYTE)*src++) != 0)
        length++;

    return length;
}

/*
 * strcpy, answering with the length rather than with the destination, which
 * is what every caller of it wanted and had to work out again.
 */
WORD strlencpy(char *dest, const char *src)
{
    WORD length = 0;

    while ((*dest++ = *src++) != 0)
        length++;

    return length;
}
