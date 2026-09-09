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

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/*
 * The console: the screen an application writes text on and the keyboard it
 * reads that text back from.
 *
 * On an ST these were the machine's own screen and keyboard, reached through
 * the VT52 in the BIOS, and they were the same screen GEM drew on. A program
 * that was both - an assembler with a GEM editor round it, a compiler run from
 * a shell - wrote its output over the desktop, waited for a key and left the
 * application to redraw. That is not an unusual thing for a program of the
 * period to do; it is what most development software did.
 *
 * There are two places it can go here and the difference is not a preference.
 * With a compositor there is a window to put a console in, and that is the
 * faithful answer: the text is drawn with the machine's own font, in the Atari
 * character set, with the escape sequences meaning what they meant. Without
 * one there is the terminal the emulator was started from, which is where
 * every console program's output has always gone and where a test reads it.
 *
 * Both are the same console as far as an application is concerned. Everything
 * below answers the same way whichever is underneath, which is the point:
 * nothing in GEMDOS or the BIOS has to know which one this is.
 */

/* A byte to the console. The VT52 reads it, so an escape sequence written a
 * byte at a time works the way one written whole does. */
void console_out(int ch);

/* Whether a key is waiting, which is what Cconis and Bconstat answer */
int console_ready(void);

/*
 * The next key, as GEMDOS reports one: the character in the low byte and the
 * IKBD scan code in bits 16 to 23.
 *
 * Waits for one when wait is set. Without it, 0 comes back when there is
 * nothing there, which is what Crawio(0xff) is for.
 */
uint32_t console_key(int wait);

/* And a line of them, with the editing GEMDOS did: Cconrs. Answers how many
 * characters were read. */
int console_line(char *buffer, int max);

/* The cursor, which is XBIOS Cursconf */
int16_t console_cursor(int16_t function, int16_t operand);

/*
 * What the console has on it, while it is what somebody is looking at, and
 * nothing the rest of the time.
 *
 * A screenshot is of whatever is on top - a menu over a dialog over the screen
 * - and a console is above all of those while it is up, being what the program
 * dropped to. This is how gem_present is told so, and it is also how the
 * console is looked at on a machine with no desktop to look at it on.
 */
struct surface;
struct surface *console_showing(void);

/*
 * The application has gone back to waiting for GEM, so whatever it had to say
 * on the console has been said.
 *
 * A console window is taken away here rather than when the last character was
 * written, because a program writing to the console is not finished with it
 * until it stops. On an ST this is where the application redrew its windows
 * over the text; here the text has a window of its own and the window goes.
 */
void console_settle(void);

/* Everything a child of fork inherited and owns none of - see gem_forget */
void console_forget(void);

/* And the end of the run: the window taken away and the terminal put back the
 * way it was found */
void console_close(void);

#endif /* CONSOLE_H */
