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

#ifndef CALLTRACE_H
#define CALLTRACE_H

#include <stddef.h>
#include <stdint.h>

/*
 * One line for each OS call a program makes, at -vv: the call as the program
 * made it and what it was answered, the way strace shows a Unix program's.
 *
 *     Fopen("NOSUCH.TXT", 0) = -33 EFILNF
 *
 * Which is the whole of what is usually wanted from a trace - which call
 * failed, with what, and why - on one line that can be searched for. The
 * lines that say a call was entered, and the details a call says about
 * itself on the way through, are still there; this is the summary after them.
 *
 * What the arguments are is a spec beside each call in the trap's table, a
 * character for each argument in the order they are on the stack:
 *
 *     w  a word, as a signed number        l  a long, as a signed number
 *     x  a word, in hex                    L  a long, in hex
 *     c  a word that is a character        p  a long that is an address
 *     s  a long that is the address of a string, shown as the string
 *
 * And after an '=', what the answer is, when it is not a number that may be
 * an error: 'a' an address, 'x' a word or long in hex, 'k' a key with its
 * scan code, and '-' for a call that does not come back, which is shown
 * before it is made because there is no after. A call with no spec is shown
 * as name(...).
 *
 * The arguments are read before the call, because some calls move the stack
 * they are on - Super does - and some change what they point at.
 */

/* The arguments of the call on the stack, by spec, into buf */
void calltrace_args(char *buf, size_t size, const char *spec);

/* Whether a call is one that does not come back, so is shown beforehand */
int calltrace_no_return(const char *spec);

/* The line, once the call has answered r; or beforehand, for one that will
 * not answer at all */
void calltrace_line(const char *name, const char *args, const char *spec,
                    uint32_t r);

#endif /* CALLTRACE_H */
