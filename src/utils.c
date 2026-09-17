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

#include "utils.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

/*
 * Both of these build their answer a byte at a time out of the one they were
 * given, lowest byte first, shifting what they have so far up to make room.
 *
 * Which means the first shift is of something nothing has been put in yet.
 * Started at nought that is nought; started at whatever was on the stack it is
 * rubbish, and the only reason the answer came out right anyway is that two
 * more shifts of eight push a word's worth of rubbish back off the top. That
 * held for as long as nobody compiled this with optimisation on. A compiler is
 * entitled to assume a variable is never read before it is written and to do
 * as it likes with code that does, which in a build with -O is what these
 * were - and what they are read for is the size of a program's text, data and
 * symbols, and every relocation in it.
 */
uint16_t endianize_16(uint16_t in)
{
    uint16_t out = 0;
    int i;

    for(i=0; i<2; ++i)
    {
        out = out << 8;
        out = out | (0xff&in);
        in = in >> 8;
    }

    return out;
}

uint32_t endianize_32(uint32_t in)
{
    uint32_t out = 0;
    int i;

    for(i=0; i<4; ++i)
    {
        out = out << 8;
        out = out | (0xff&in);
        in = in >> 8;
    }

    return out;
}
