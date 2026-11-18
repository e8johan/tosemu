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

#ifndef VT52ANSI_H
#define VT52ANSI_H

#include <stddef.h>

/*
 * What an ST's console output means, said to a terminal of today.
 *
 * A TOS program writes for the VT52 in the BIOS and for the Atari character
 * set, and a terminal speaks ANSI and UTF-8. So a program that clears the
 * screen with ESC E has the terminal print an E, and an accented letter is a
 * byte no terminal has a glyph for. This turns the one into the other, a byte
 * at a time, the way EmuTOS's CONF_SERIAL_CONSOLE_ANSI does for its serial
 * console: each escape a terminal can express becomes the ANSI one, the rest
 * are dropped, the control characters the VT52 did nothing with are dropped
 * with them, and a character from the top half of the Atari set becomes its
 * UTF-8.
 *
 * It is only for a terminal somebody is looking at. Output going into a file
 * or a pipe goes as it was written - that is what a Makefile reading a
 * program's listing wants - so console.c asks whether standard output is a
 * terminal before using this.
 */

struct vt52_ansi
{
    int state;          /* where in an escape sequence the last byte left it */
    int row;            /* ESC Y's row, while its column is awaited */

    /* What the program changed about the terminal that outlives it, to be
     * put back at the end */
    int cursor_hidden;
    int unwrapped;
    int reversed;
};

/* As much as one byte can come to, an escape with two numbers in it, or the
 * putting back of all three things a program can leave changed */
#define VT52_ANSI_MAX (24)

/*
 * One byte of a program's output, translated into out, which has room for
 * VT52_ANSI_MAX. Answers how many bytes were put there, which is nought for a
 * byte that is part of a sequence still being read or that is dropped.
 */
size_t vt52_ansi_byte(struct vt52_ansi *t, unsigned char ch, char *out);

/* What puts back whatever the program left changed, into out, as above */
size_t vt52_ansi_restore(struct vt52_ansi *t, char *out);

#endif /* VT52ANSI_H */
