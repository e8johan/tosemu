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
 * It starts the first time a program takes the video hardware over, which it
 * does by pointing the base anywhere other than the screen the machine was
 * built with, or by setting the resolution. When the base goes back there, the
 * screen has been handed back to whatever that screen stands for - GEM's
 * windows and the console, which are on the desktop already - and after a
 * moment the picture steps aside for them, as long as there is something of
 * theirs up to step aside for. It comes back when the base moves away again. A
 * debugger does both every time it starts the program it is debugging and every
 * time the program stops - which somebody using one may not want, so it can be
 * told to stay up instead; see TOSEMU_PICTURE in settings.c.
 *
 * The resolution is the other half of what the hardware shows, and it moves
 * this picture and nothing else. GEM's screen is a surface made once when GEM
 * starts and is never reshaped, so an application's windows, its dialogs and
 * the workstation the VDI opens for it all go on being the size they were, and
 * Getrez goes on answering for that screen rather than for the mode register -
 * see the note at the top of xbiosscreen.c for why those are answered apart.
 * Setting the mode cannot be undone the way moving the base can, so a program
 * that has set it keeps the picture for the rest of the run; hardware_taken_over
 * in video.c says why.
 *
 * Or to be up from the start, which is the other program that draws for
 * itself: one that never moves the base at all, because the screen the machine
 * came with is the screen it draws on. There is no moment there where it says
 * it has taken the hardware over - it just writes where Physbase pointed - so
 * nothing but being told shows it, and being told is what `always` is.
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
 * on the screen rather than on a terminal. With `always` that is every program
 * from the start, the picture being the screen from the start */
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
