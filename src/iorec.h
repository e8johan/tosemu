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

#ifndef IOREC_H
#define IOREC_H

#include <stdint.h>

/*
 * The ring buffer an _IOREC describes, as arithmetic.
 *
 * A device that interrupts fills one of these from its handler and whatever
 * reads the device empties it. That much is an ordinary ring; what makes it
 * worth a file of its own is that the ring is not ours. It lives in the
 * emulated machine's memory, Iorec hands its address to the application, and
 * period software reads it directly rather than going through Bconin - which
 * was the fast way to take MIDI in and is what anything watching a stream of
 * notes did.
 *
 * So the rule is not "a ring that works" but "the ring TOS had", down to which
 * slot a byte goes in. Two things follow from that and both are easy to get
 * wrong:
 *
 * The index is advanced *before* the byte is touched. head and tail name the
 * slot last read and last written, not the next ones, so a byte is written to
 * buf[tail + 1] and read from buf[head + 1]. Storing at the old index instead
 * would work perfectly against our own reader and hand an application reading
 * the record itself every byte one slot out of step.
 *
 * A full ring drops what it is given. There is no waiting available - the
 * writer is an interrupt handler - and no overwriting either, because one slot
 * is always left empty and that is what makes head equal to tail mean empty
 * rather than full.
 *
 * Both of those are read off _midivec in EmuTOS's bios/aciavecs.S and bconin3
 * in bios/midi.c, which are the two halves of the same buffer.
 */

struct iorec_ring {
    uint8_t *buf;
    int size;
    int head;
    int tail;
};

/* A byte from the device. Answers 0 when the ring was full and it was
 * dropped, which is the only thing a device can do about it. */
int iorec_push(struct iorec_ring *r, uint8_t byte);

/* And a byte out of it. Answers 0 when there was nothing waiting. */
int iorec_take(struct iorec_ring *r, uint8_t *byte);

/* How many are waiting, which is what Bconstat answers with */
int iorec_count(const struct iorec_ring *r);

/* One less than the size, there being a slot that is never filled */
int iorec_capacity(const struct iorec_ring *r);

#endif /* IOREC_H */
