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

#ifndef PRINTER_H
#define PRINTER_H

/*
 * A printer, on the host.
 *
 * This is the half of printing that has nothing to do with GEM: a sheet of
 * paper of a given size at a given number of dots to the inch, somewhere to
 * draw on it, and CUPS at the end to put ink on it. What turns VDI calls into
 * marks on the page is src/emuvdi/prndev.c, which is the other half.
 *
 * A page is a bitmap and not a list of shapes, and that is what a GDOS printer
 * driver was too. The drivers Atari shipped rendered a page into a band of
 * memory and sent the rows to the printer; nothing about the VDI is a
 * description of a drawing that survives being drawn. So the whole of the
 * EmuTOS VDI serves a printer exactly as it serves the screen - the only
 * difference is which memory it draws into and how many dots to the inch that
 * memory stands for - and none of it has to be written twice.
 *
 * It is on this side of emuvdi rather than inside it, the way fontface.c is,
 * because that half is built against EmuTOS's headers where WORD, LONG and
 * string.h are EmuTOS's own. So this is written in plain types.
 */

/*
 * Somewhere to draw a page, and how large it is.
 *
 * The memory is Atari planar in host byte order, the same shape as a surface,
 * because the VDI is going to be pointed straight at it. A row is a whole
 * number of words, which is why the width is rounded down to a multiple of
 * sixteen - the VDI works a row's length out by dividing and the caller works
 * it out by multiplying, and the two only agree on a multiple of sixteen.
 *
 * The page is made the first time this is called and kept afterwards, so every
 * workstation opened on the printer draws on the same sheet. Answers null when
 * the settings name a page that cannot be made, having said why.
 */
void *printer_page(int planes, int *width, int *height, int *words_per_line);

/* How many dots to the inch that page stands for, which is what decides how
 * large a point is on it and therefore how tall the text comes out */
int printer_dpi(void);

/* Blanks the page. v_clrwk does this through the VDI, which knows how; this is
 * for the times something else has to, such as after a page has been sent. */
void printer_page_clear(void);

/*
 * Adds what is on the page to the job, starting one if none is under way.
 *
 * The palette says what each pen looks like - entry n is 0x00rrggbb for pen n,
 * and there are as many as the page has colours - because a surface holds pens
 * rather than colours and only the VDI knows which is which.
 *
 * Answers 0 having said why if the page could not be added, in which case the
 * job is abandoned rather than left half written.
 */
int printer_page_out(const unsigned int *palette, int colours);

/* Whether a job has been started and not yet sent */
int printer_job_started(void);

/*
 * Ends the job and hands it over: to CUPS, through lp, or to the file the
 * settings named. Answers 1 if it went, 0 having said why if it did not.
 * Ending a job nothing was added to is nothing to do and succeeds.
 */
int printer_job_end(void);

/*
 * Lets go of a job a child of fork inherited without disturbing it.
 *
 * Pexec forks, and the child has a copy of everything the parent had open -
 * including, if the parent was printing, a half written job and the path it is
 * being written to. Neither is the child's. Ending it would print a second
 * copy of the parent's document and throwing it away would delete the file the
 * parent is still writing, so the child does neither: it forgets that there is
 * one, and the parent goes on printing.
 */
void printer_job_forget(void);

#endif /* PRINTER_H */
