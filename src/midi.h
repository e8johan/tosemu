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

#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>

/*
 * The MIDI port: the socket on the back of an ST that a synthesiser is plugged
 * into, and what stands in for it here.
 *
 * On the machine this was an ACIA with a current loop either side of it,
 * running at 31250 baud, and nothing above it knew any more than that. A
 * program sent bytes and bytes arrived; what they meant was between it and
 * whatever was on the other end of the cable. That is worth saying because it
 * decides the shape of everything below: this is a byte pipe and not a MIDI
 * library, and it parses nothing it is given.
 *
 * Three places the bytes can go, and the setting picks by how it is spelled.
 *
 * `hw:1,0,0` is an ALSA raw device, which is the closest thing the host has to
 * the cable. Bytes go out and come back untouched - running status as the
 * program sent it, active sensing, a sysex dump of any length - because
 * nothing in the path looks at them. It reaches the interface plugged into the
 * machine and not much else.
 *
 * `seq:20:0`, or `seq:` and a port's name, is the ALSA sequencer. That is a
 * port with a name, which appears in aconnect and in whatever patchbay
 * somebody uses, and it reaches software as readily as hardware. `seq:` on its
 * own makes the port and connects it to nothing, for something else to connect
 * to afterwards. The cost is that the sequencer is event shaped rather than
 * byte shaped, so the bytes are assembled into events on the way out and taken
 * apart again on the way in, and running status is regenerated rather than
 * passed through.
 *
 * `file:sent.bin` is neither, and it is not a lesser port. It is how the
 * traffic is looked at without a synthesiser to look at it on - by a test, on
 * a machine with no sound hardware, or by somebody who wants to see what a
 * program actually sent. The same argument as the printer's file destination.
 *
 * Nothing configured is the fourth case and the default one: there is no port,
 * every byte written is discarded, and nothing ever arrives. That is what an
 * ST with no cable in the socket did, and it is what the test suite runs as.
 *
 * The prefix is not optional, and that is worth a sentence because it looks
 * like it should be. A sequencer port named rather than numbered has nothing
 * in its spelling to mark it as one, so a spelling with no prefix would have
 * to be read as a port name - and a mistyped `hw:` would then quietly become a
 * sequencer port connected to nothing, which from the outside is
 * indistinguishable from a working port with a silent synthesiser on the end.
 */

/*
 * Open whatever the setting asked for. Answers whether there is a port, which
 * is not the same as whether opening one failed: asking for nothing succeeds
 * and leaves no port behind.
 */
int midi_open(void);

/* Whether there is one, which is what the BIOS asks before it bothers */
int midi_wanted(void);

/*
 * A descriptor to sleep on, or -1 when there is nothing worth sleeping on.
 *
 * The file backend answers -1 on purpose although it has a descriptor: a
 * regular file is always ready to be read, so putting one in a poll turns a
 * wait into a spin. What it has to say it has said by the time it is opened.
 */
int midi_fd(void);

/*
 * Move whatever can be moved, in both directions, without ever waiting. This
 * is the only call that touches the host; everything else is the two rings it
 * fills and drains.
 */
void midi_pump(void);

/* One byte from the host, or 0 when none is waiting */
int midi_take(uint8_t *byte);

/*
 * One byte to the host. Answers 0 when it was dropped rather than sent, which
 * happens when the far end has stopped reading and the ring has filled.
 *
 * Dropping is the only honest answer available. Waiting would stop the
 * emulated machine on somebody else's program, and a MIDI byte is worth less
 * than the machine: what is lost is a note, and what waiting would lose is the
 * sequencer that was playing it.
 */
int midi_give(uint8_t byte);

/*
 * Forget a half finished message and everything queued either way.
 *
 * A program that has gone took its running status and its half sent sysex with
 * it, and the next one starts on a port that owes the synthesiser nothing.
 * Without this a dump cut in half by a Pexec would be finished by whatever the
 * next program sent first.
 */
void midi_reset(void);

/* Everything a child of fork inherited and owns none of - see gem_forget */
void midi_forget(void);

/* The end of the run: what is queued goes, and the port is let go of */
void midi_close(void);

/* What it was told to open, for saying so when something is wrong */
const char *midi_named(void);

#endif /* MIDI_H */
