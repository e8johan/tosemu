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

#ifndef MFP_H
#define MFP_H

#include <stdint.h>

/*
 * The 68901, which is the chip that decides when the machine is interrupted
 * and by what.
 *
 * Sixteen channels, numbered 0 to 15, each of which can be enabled, can be
 * pending, can be masked and can be in service. The highest numbered pending
 * channel wins. Four of the sixteen are timers of its own and the rest are
 * wires from elsewhere in the machine - the ACIAs, the disk, the serial port.
 *
 * This is here because a MIDI program of the period programs it directly. A
 * sequencer sets Timer A going at whatever its tempo works out to and hangs
 * its own handler on the vector, because Xbtimer and the rest of the BIOS are
 * for setting a thing up once and this is a thing that has to happen five
 * thousand times a second. Emulating the chip rather than the call is the only
 * way software written like that can run at all.
 *
 * Two departures from the obvious reading, both of which real handlers depend
 * on and both of which were read off EmuTOS rather than guessed:
 *
 * Channels 8 to 15 live in the A registers and 0 to 7 in the B registers, so
 * Timer A - channel 13 - is bit 5 of IERA, and the ACIA - channel 6 - is bit 6
 * of IERB. The letters name which half of the sixteen, not which timer.
 *
 * Writing a pending or in-service register clears the bits written as nought
 * and leaves the rest alone, rather than storing what was written. That is how
 * a handler clears its own channel without disturbing anything that came in
 * while it was running, and every handler does it: TOS's own ACIA handler ends
 * by writing 0xBF to ISRB to clear bit 6.
 */

#define MFP_BASE_ADDRESS (0xFFFA00)
#define MFP_LENGTH       (0x30)

/* The channels this machine has anything wired to, named as EmuTOS names them
 * in include/biosdefs.h */
#define MFP_TIMER_D (4)
#define MFP_200HZ   (5)  /* Timer C, which is the system clock */
#define MFP_TIMER_C (5)
#define MFP_ACIA    (6)  /* Both of them, through the fourth general purpose
                          * input - they share a channel */
#define MFP_TIMER_B (8)
#define MFP_TIMER_A (13)

/* The general purpose input the ACIAs are wired to. Active low: the bit reads
 * as nought while a chip is asking for attention. */
#define MFP_GPIP_ACIA (4)

/* What a timer is called where one of the four is asked for by number, which
 * is how Xbtimer names them */
#define MFP_TIMER_COUNT (4)

void mfp_reset(void);

/* A register, addressed as the machine addresses it: an offset from
 * MFP_BASE_ADDRESS. The chip is on the low half of the bus, so every register
 * is at an odd offset and the even ones are not anything. */
uint8_t mfp_read_at(uint32_t offset);
void    mfp_write_at(uint32_t offset, uint8_t value);

/*
 * Something happened on a channel. Whether that becomes an interrupt is the
 * chip's business: a channel whose enable bit is clear is not listening, and
 * nothing is remembered about what it missed.
 */
void mfp_raise(int channel);

/*
 * A general purpose input changed. The chip decides whether that edge is the
 * interesting one - which is what the active edge register is for - and raises
 * the matching channel if it is.
 */
void mfp_gpip(int bit, int high);

/* The channel that should be interrupting, or -1. Pending, not masked out, and
 * not sitting underneath something already being serviced. */
int mfp_pending_channel(void);

/*
 * The same question, asked by a processor that is taking the interrupt: the
 * channel stops being pending, starts being in service, and the vector it
 * should go through comes back. Answers -1 when nothing was pending after all.
 */
int mfp_acknowledge(void);

/* Which of the two hundred and fifty six vectors a channel goes through, which
 * is the vector register's top four bits and the channel in the bottom four */
int mfp_vector_of(int channel);

/*
 * A channel's handler has finished, which clears its in-service bit.
 *
 * A handler on the machine does this for itself by writing its own bit away -
 * that is what the software end of interrupt arrangement means, and every TOS
 * handler ends with such a write. This is for the channels tosemu answers for
 * itself, where there is no handler to do it: a bit left set holds off that
 * channel and every lower one for ever, which looks exactly like the device
 * having gone quiet.
 */
void mfp_finished(int channel);

/* What Jenabint and Jdisint do. Disabling clears everything the channel had:
 * a channel that is turned off and on again has forgotten what it missed. */
void mfp_enable(int channel);
void mfp_disable(int channel);

/* What Xbtimer does, and it writes the registers exactly as EmuTOS's
 * setup_timer does - timers C and D sharing one control register between them */
void mfp_setup_timer(int timer, uint8_t control, uint8_t data);

/* Which channel a timer interrupts on, 0 to 3 being A to D */
int mfp_timer_channel(int timer);

/*
 * How long a timer runs for, in nanoseconds.
 *
 * Zero says it is stopped, which is what a control field of nought means -
 * not a divider of one. A negative answer says it is counting something
 * rather than waiting: the two upper modes count edges on a pin and measure
 * how long a pulse lasts, and neither is a length of time this can work out.
 *
 * The control argument is the field as it appears for timers A and B, which is
 * four bits. Timers C and D have three, sharing a register, and mfp_timer_period
 * is what knows where to find each of them.
 */
long mfp_period_from(uint8_t control, uint8_t data);
long mfp_timer_period(int timer);

/* For the checks, and for anyone wanting to know what the chip thinks: the
 * registers as the machine would read them */
uint8_t mfp_register(uint32_t offset);

#endif /* MFP_H */
