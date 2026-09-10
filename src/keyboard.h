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

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* The bits a caller says a modifier is held with, which are the bits GEM
 * reports them in, include/biosdefs.h */
#define KEYBOARD_RSHIFT (0x01)
#define KEYBOARD_LSHIFT (0x02)
#define KEYBOARD_SHIFT  (KEYBOARD_RSHIFT|KEYBOARD_LSHIFT)
#define KEYBOARD_CTRL   (0x04)
#define KEYBOARD_ALT    (0x08)

/*
 * What a key press becomes, which is one word: the scan code of the key in the
 * top half and the character it typed in the bottom.
 *
 * The scan code is a place on an ST keyboard and the character is a byte of the
 * ST's alphabet, so the caller hands over both halves as the host reports them
 * - a scan code from where the key is, and the character as Unicode - along
 * with which modifiers were held. Nought is answered for a press there is no
 * way to describe, which is neither a key an ST has nor a character it has a
 * byte for.
 */
uint16_t keyboard_word(uint16_t scancode, uint32_t codepoint, uint16_t held);

#endif /* KEYBOARD_H */
