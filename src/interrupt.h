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

#ifndef INTERRUPT_H
#define INTERRUPT_H

/*
 * The part of the machine that knows there is a 68000 in it.
 *
 * mfp.c and acia.c are the chips as registers and rules, and they are host C
 * that could be checked on a machine with no emulator in it at all. This is
 * where they are wired up: to the memory map, so that a program can read and
 * write them; to a clock, so that a timer told to wait actually waits; and to
 * the processor, so that what the chip decides becomes something the program
 * has to deal with.
 *
 * The line is worth keeping. Everything on the other side of it can be asked a
 * question directly - what does a divider of sixty four and a count of a
 * hundred and ninety two come to, which channel wins between 13 and 6 - and
 * that is what bin/miditest does. Nothing here can be asked anything without a
 * machine to ask it in.
 *
 * None of this exists unless it was asked for. An ordinary run has no timers,
 * no interrupts and no chips in the memory map, exactly as before, because the
 * overwhelming majority of TOS programs neither want nor tolerate a machine
 * that interrupts them. It is turned on by asking for a MIDI port - a program
 * given one is a program that will be programming timers - or by saying so.
 */

/*
 * Whether this machine has any of it. Settled once, from the settings, and the
 * same answer for the life of the run.
 */
int interrupt_wanted(void);

/*
 * Build it: the chips into the memory map, the clock started, and the system
 * timer set going the way TOS would have left it.
 *
 * Called where the rest of the machine's memory is laid out, because that is
 * what it adds to and because a machine built a second time - which is what
 * Pexec does - has to have it again.
 */
void interrupt_init(void);

/* The end of an application and the start of another's: the vectors, the
 * pending bits and the running timers all belonged to the one that has gone */
void interrupt_reset(void);

/* A descriptor worth sleeping on, or -1. It is MIDI's: nothing else here
 * arrives from outside, a timer being something that is waited for rather
 * than something that happens. */
int interrupt_fd(void);

/*
 * How long until the next thing is due, in milliseconds, or -1 when nothing
 * is. What wants to know is anything about to sleep: no 68000 instruction runs
 * while the emulator is in poll, so a timer whose moment passes in there would
 * not go off until something else happened to wake it.
 */
long interrupt_next_due_ms(void);

/*
 * Look at the clock and do whatever has come due. Cheap to call and called
 * very often - see the throttle in interrupt_tick, which is what actually
 * decides how often.
 */
void interrupt_service(void);

/* The same, from the instruction hook, and throttled so that it is a counter
 * decrement rather than a clock read on almost every instruction */
void interrupt_tick(void);

/* A control register changed, so whatever was worked out about how long the
 * timers run for is no longer true */
void interrupt_timers_changed(void);

#endif /* INTERRUPT_H */
