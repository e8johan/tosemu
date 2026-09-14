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

#ifndef ACIA_H
#define ACIA_H

#include <stdint.h>

/*
 * The two 6850s: the one the keyboard processor talks through and the one the
 * MIDI socket is wired to.
 *
 * Each is two registers a byte wide. Writing the first sets the chip up and
 * reading it back gives the status instead; the second is the byte itself,
 * going out when written and coming in when read. They sit on the upper half
 * of the bus, so each register is at an even address with the odd byte beside
 * it reading as nothing.
 *
 * This exists because a MIDI program of the period does not ask the BIOS for
 * anything. It reads 0xFFFC04 to see whether a byte has arrived and 0xFFFC06
 * to take it, in an interrupt handler it installed itself, because that was
 * the only way to keep up with a stream of notes on an eight megahertz machine.
 * Bconin is the slow path nobody serious used.
 *
 * The keyboard one is modelled as well, and that is not thoroughness. TOS's
 * interrupt handler services both ACIAs on every interrupt from either - see
 * _int_acia in EmuTOS's bios/aciavecs.S, which calls midisys and then ikbdsys
 * without asking which of them interrupted. An unmapped address at 0xFFFC00
 * stops the emulator dead, so the keyboard ACIA has to be there to be read
 * even though nothing will ever arrive from it.
 */

#define ACIA_BASE_ADDRESS (0xFFFC00)
#define ACIA_LENGTH       (8)

#define ACIA_IKBD (0)
#define ACIA_MIDI (1)

/* The status bits, named as EmuTOS names them in bios/acia.h */
#define ACIA_RDRF (1 << 0) /* Receive data register full */
#define ACIA_TDRE (1 << 1) /* Transmit data register empty */
#define ACIA_OVRN (1 << 5) /* A byte arrived before the last was read */
#define ACIA_IRQ  (1 << 7) /* This chip is asking for attention */

/* And the control bits worth knowing about */
#define ACIA_RESET   (3)        /* In the two divider bits, a master reset */
#define ACIA_RIE     (1 << 7)   /* Interrupt when a byte arrives */
#define ACIA_TIE     (1 << 5)   /* Interrupt when the transmitter is free, in
                                 * the two transmit control bits */

void acia_reset(void);

/*
 * A register, addressed the way the machine addresses it: an offset from
 * ACIA_BASE_ADDRESS. Which chip and which register are worked out from it, and
 * an odd offset reads as nothing.
 */
uint8_t acia_read_at(uint32_t offset);
void    acia_write_at(uint32_t offset, uint8_t value);

/* Or by name, which is how everything but the memory map wants it. reg is 0
 * for the control and status register and 1 for the data register. */
uint8_t acia_read(int which, int reg);
void    acia_write(int which, int reg, uint8_t value);

/*
 * Whether either chip is asking for attention, which is the one thing the MFP
 * wants to know: both of their interrupt lines are wired together into its
 * fourth general purpose input.
 */
int acia_interrupting(void);

/* Whether a byte may be handed to it, and handing one over. A byte given to a
 * chip that already holds one unread is what sets the overrun bit. */
int  acia_can_receive(int which);
void acia_receive(int which, uint8_t byte);

/* And the other direction: what the machine has written to be sent. Answers 0
 * when there is nothing waiting. */
int acia_take_transmitted(int which, uint8_t *byte);

#endif /* ACIA_H */
