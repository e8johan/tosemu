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

#ifndef EMUVDI_PRNDEV_H
#define EMUVDI_PRNDEV_H

/*
 * A printer, as a VDI device.
 *
 * An application prints by opening a workstation on a device that is not the
 * screen - v_opnwk with 21 in work_in[0], which is where GDOS's ASSIGN.SYS put
 * the printer - drawing the page with the same calls it draws a window with,
 * and calling v_updwk to say that the page is finished. That is the whole of
 * the interface, and it is why printing needs no drawing code of its own: the
 * VDI already draws everything, and a printer is a bitmap of a different size.
 *
 * So this is not a driver. It is the bookkeeping around one: which handles
 * belong to the printer, pointing the VDI at the page instead of the screen
 * for the length of a call, and the four calls a printer answers that a screen
 * does not. src/printer.c is the sheet of paper at the other end.
 *
 * This header is emuvdi talking to itself, the way gdos.h is, and is written
 * in EmuTOS's types because what it deals in are the VDI's own arrays.
 */

/*
 * Points the VDI at the printer's page, if this call is one addressed to it.
 *
 * Answers 1 when it did, and the caller must then call prndev_unbind when the
 * call has been served - everything from the surface the VDI draws into to
 * which workstation line-A thinks is current has been changed and has to be
 * put back.
 *
 * A call is addressed to the printer when its handle is one of the printer's.
 * v_opnwk is the exception and is not bound here: it arrives before there is a
 * handle to recognise, so prndev_served does the whole of it.
 */
int prndev_bind(WORD *control);

/* And back to the screen. The control array is read rather than only the
 * saved state, because a call that opened a workstation says in it which
 * handle that turned out to be. */
void prndev_unbind(WORD *control);

/*
 * The calls a printer answers for itself. Answers 1 when the call has been
 * dealt with here and EmuTOS is not to see it.
 *
 * Four of them are printing rather than drawing - putting a page out, ending a
 * page, blanking one and finishing the job - and one is v_opnwk, which cannot
 * reach EmuTOS because EmuTOS has exactly one physical workstation and opening
 * it again would throw away the screen's.
 */
int prndev_served(WORD *control, WORD *intin, WORD *intout, WORD *ptsout);

/*
 * Every printer workstation forgotten and whatever job was under way sent.
 *
 * This is for the machine going away with a workstation still open, which is
 * a program that printed and did not close what it printed through. Sending
 * what it has is the useful answer: the pages in the job are ones the program
 * asked for, and throwing them away because it did not say goodbye tidily
 * would lose work rather than tidy up.
 */
void prndev_reset(void);

/*
 * The same, for a child of fork: everything forgotten and nothing sent.
 *
 * Pexec forks, and the child has a copy of the parent's open workstations, the
 * parent's half drawn page and the parent's half written job. None of it is
 * the child's. Sending it would print a second copy of somebody else's
 * document, so it is put down rather than finished.
 */
void prndev_forget(void);

#endif /* EMUVDI_PRNDEV_H */
