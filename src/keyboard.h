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

/*
 * A key held down, typing itself again.
 *
 * The keyboard only ever says that a key went down and that it came up. The
 * repeating is TOS's, done on its 50Hz tick, and so are the two numbers it goes
 * by: how many ticks before the first repeat and how many between the rest,
 * which a program reads and sets with XBIOS Kbrate. A delay of nought is no
 * repeat at all and a rate of nought is one repeat and no more, because that is
 * what TOS's counter does with them.
 *
 * Which key is held is the host's number for it; the caller turns it into a
 * word each time, so that a modifier pressed while it is held counts, the way
 * it does in EmuTOS. Times are milliseconds on any clock that only goes
 * forwards.
 */

/* XBIOS Kbrate: a negative leaves that one as it is, and what comes back is
 * the two as they were, the delay in the top byte */
uint16_t keyboard_rate(int16_t delay, int16_t rate);

/* What the desktop would like, as it says it: milliseconds before the first
 * repeat and repeats a second, nought being none. Taken only until a program
 * sets its own. */
void keyboard_rate_preferred(int32_t delay_ms, int32_t per_second);

void keyboard_held(uint32_t key, long long now);
void keyboard_released(uint32_t key);
void keyboard_let_go(void);

/* Whether the key held is due to type again, saying which it is. One repeat is
 * answered however late this is asked, and the next is counted from then. */
int keyboard_repeat_due(long long now, uint32_t *key);

/* Milliseconds until it is, or -1 when nothing is going to repeat */
long keyboard_repeat_next(long long now);

#endif /* KEYBOARD_H */
