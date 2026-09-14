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

/* See acia.h for what these are and why both of them are here. */

#include "acia.h"

#define ACIAS (2)

struct acia {
    uint8_t control;
    uint8_t status;
    uint8_t received;   /* The byte waiting to be read */
    uint8_t sending;    /* And the one waiting to be sent */
    int     holding;    /* Whether there is one to send */
};

static struct acia acias[ACIAS];

/*
 * The transmitter is always empty and the receiver never is by accident.
 *
 * TDRE stays set because there is nothing on this side that takes time: what
 * the machine writes is put in a ring and sent by the host when it gets round
 * to it, so a program polling for the transmitter to clear would be waiting
 * for a busy signal that is never coming. On the real chip it cleared for the
 * length of one byte at 31250 baud, which is a third of a millisecond, and
 * every program of the period polls it rather than assuming it.
 */
static void settle(struct acia *a)
{
    a->status &= (uint8_t)~ACIA_IRQ;
    a->status |= ACIA_TDRE;

    /* The chip raises its interrupt line when it has something to say and has
     * been told to say so. Only the receive half is modelled - the transmit
     * interrupt would fire continuously, the transmitter never being busy. */
    if ((a->control & ACIA_RIE) && (a->status & (ACIA_RDRF | ACIA_OVRN)))
        a->status |= ACIA_IRQ;
}

void acia_reset(void)
{
    int i;

    for (i = 0; i < ACIAS; i++)
    {
        struct acia *a = &acias[i];

        a->control = 0;
        a->status = 0;
        a->received = 0;
        a->sending = 0;
        a->holding = 0;

        settle(a);
    }
}

uint8_t acia_read(int which, int reg)
{
    struct acia *a;

    if (which < 0 || which >= ACIAS)
        return 0;

    a = &acias[which];

    if (reg == 0)
        return a->status;

    /*
     * Reading the byte is what clears the receiver, and that is the whole of
     * how a handler gets out of its loop: TOS reads until the chip stops
     * asking, so a data register that did not clear on being read would leave
     * it reading the same byte for ever.
     */
    a->status &= (uint8_t)~(ACIA_RDRF | ACIA_OVRN);
    settle(a);

    return a->received;
}

void acia_write(int which, int reg, uint8_t value)
{
    struct acia *a;

    if (which < 0 || which >= ACIAS)
        return;

    a = &acias[which];

    if (reg == 0)
    {
        /*
         * A master reset, which is what the two divider bits both being set
         * means rather than any sort of divider. It is the first thing TOS
         * writes to either chip, and what it is for is throwing away whatever
         * arrived while nobody was listening.
         */
        if ((value & 3) == ACIA_RESET)
        {
            a->status = 0;
            a->received = 0;
            a->holding = 0;
        }

        a->control = value;
        settle(a);
        return;
    }

    /*
     * A byte to send. The one before it has gone whether or not anybody
     * collected it, which is what a transmitter that is never busy comes to -
     * see settle. Nothing that writes here writes twice without looking,
     * every program of the period having polled TDRE first.
     */
    a->sending = value;
    a->holding = 1;

    settle(a);
}

/* Which chip and which register an address names. The registers are a byte
 * wide on the upper half of the bus, so they are two apart and the odd
 * addresses between them are not anything. */
uint8_t acia_read_at(uint32_t offset)
{
    if (offset & 1)
        return 0;

    return acia_read((int)((offset >> 2) & 1), (int)((offset >> 1) & 1));
}

void acia_write_at(uint32_t offset, uint8_t value)
{
    if (offset & 1)
        return;

    acia_write((int)((offset >> 2) & 1), (int)((offset >> 1) & 1), value);
}

int acia_interrupting(void)
{
    int i;

    for (i = 0; i < ACIAS; i++)
        if (acias[i].status & ACIA_IRQ)
            return 1;

    return 0;
}

int acia_can_receive(int which)
{
    if (which < 0 || which >= ACIAS)
        return 0;

    /* A byte may always be handed over. One arriving on top of another is an
     * overrun rather than something to be refused - the far end of a MIDI
     * cable has no way of being told to wait, and neither had the chip. */
    return 1;
}

void acia_receive(int which, uint8_t byte)
{
    struct acia *a;

    if (which < 0 || which >= ACIAS)
        return;

    a = &acias[which];

    /* The byte that was there has been lost, and the chip says so rather than
     * quietly holding the older of the two. What a reader does about it is its
     * own business; what it must not do is believe nothing happened. */
    if (a->status & ACIA_RDRF)
        a->status |= ACIA_OVRN;

    a->received = byte;
    a->status |= ACIA_RDRF;

    settle(a);
}

int acia_take_transmitted(int which, uint8_t *byte)
{
    struct acia *a;

    if (which < 0 || which >= ACIAS)
        return 0;

    a = &acias[which];

    if (!a->holding)
        return 0;

    *byte = a->sending;
    a->holding = 0;

    settle(a);

    return 1;
}
