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

#ifndef SCRAPTEXT_H
#define SCRAPTEXT_H

#include <stddef.h>

/*
 * What a GEM application means by text, and what the desktop means by it.
 *
 * They disagree about two things at once. An ST spells a line ending CR LF and
 * a desktop spells it LF; and above 127 they share no characters at all - the
 * ST has its own set, with accented letters, Hebrew and mathematics in an
 * order nothing else uses, where the desktop has UTF-8. Neither difference is
 * hard, but both have to be undone in the same pass, and getting one right
 * while leaving the other is what makes a paste look almost correct.
 *
 * These are the only two functions that know that, deliberately: everything
 * else in the scrap moves bytes without looking at them, and the character set
 * lives here where it can be tested without a compositor or an emulator.
 */

/*
 * Both allocate what they return and both NUL terminate it, so the result can
 * be used as a string when it does not contain a NUL to begin with. The length
 * written to out_length does not count that terminator. Null means there was
 * no room.
 */

/* An ST's text as the desktop wants it: UTF-8, and LF between lines */
char *scrap_text_to_utf8(const char *atari, size_t length, size_t *out_length);

/*
 * And back. Anything the ST has no character for becomes a question mark
 * rather than being dropped, because a word with a letter missing reads as a
 * spelling mistake, and one with a question mark in it reads as what happened.
 */
char *scrap_text_from_utf8(const char *utf8, size_t length, size_t *out_length);

/*
 * The character set itself, for the test to check against the system font, and
 * for whatever else comes to need it. 0 means the ST has nothing there.
 */
unsigned scrap_text_codepoint(unsigned char atari);

#endif /* SCRAPTEXT_H */
