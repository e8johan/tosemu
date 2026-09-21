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

#ifndef VIDEO_H
#define VIDEO_H

/*
 * The picture the video hardware shows, for a program that draws its own.
 *
 * Everything GEM draws is on the VDI's surfaces, and nothing a program writes
 * into the machine's memory is ever seen. A program that takes the machine
 * over - a debugger with a screen of its own is the usual one - does exactly
 * that: it draws into a buffer and points the video base at it. An ST showed
 * whatever the base pointed at fifty times a second, and so does this: the
 * memory is brought across onto a surface of the shifter's shape and shown in
 * a window of its own.
 *
 * It starts the first time a program points the base anywhere other than the
 * screen the machine was built with. When the base goes back there, the screen
 * has been handed back to whatever that screen stands for - GEM's windows and
 * the console, which are on the desktop already - and after a moment the
 * picture steps aside for them, as long as there is something of theirs up to
 * step aside for. It comes back when the base moves away again. A debugger
 * does both every time it starts the program it is debugging and every time
 * the program stops.
 */

/* Brings the picture across now, and shows it if anything changed. Nothing
 * at all until a program has taken the video hardware over. */
void video_frame(void);

/* The same from the instruction hook, which is where a program that never
 * waits for anything is caught up with. At most fifty times a second, and
 * almost every call is a counter going down. */
void video_tick(void);

/* Whether a program has taken the video hardware over at all, which is a
 * program that has the screen the way a GEM program has - its console belongs
 * on the screen rather than on a terminal */
int video_taken(void);

/* And whether the picture is up now, in which case it is the screen: it is
 * what a screenshot is of, and what a person types at */
int video_showing(void);

/*
 * For a wait: puts the picture away if the screen has been handed back for
 * long enough, and otherwise says how many milliseconds until it will have
 * been, or -1 when nothing is coming. A wait that would sleep past it gives up
 * early and asks again, the way it does for gfx_settle.
 */
long video_settle(void);

/* Lets go of the picture a child of fork inherited, the way gem_forget lets
 * go of everything else of the parent's */
void video_forget(void);

#endif /* VIDEO_H */
