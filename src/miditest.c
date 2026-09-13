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

/*
 * What the MIDI port does with the bytes it is handed, checked without a
 * synthesiser to hear them.
 *
 * tests/c-midi.c checks the port from inside the emulator, where a program can
 * write to it through the BIOS and see that the right bytes came out the other
 * end. What it cannot check is what happens when things go wrong: a far end
 * that stops reading halfway through a dump is not something an application
 * can arrange, and neither is a port that was never there.
 *
 * So the rings are checked here, and they are what is worth checking. Every
 * byte in both directions goes through one, the emulated machine and the host
 * each being something the other may not be made to wait for, and the two
 * failure modes - a full ring and an empty one - are the ones a test under the
 * emulator would have to send several thousand notes to reach.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "midi.h"
#include "settings.h"

#define SENT    "build/miditest-sent.bin"
#define ARRIVED "build/miditest-arrived.bin"

/* What the rings hold, less the slot that is always left empty. midi.c says
 * why there is one; this is the number that follows from it. */
#define CAPACITY (8192 - 1)

static int n;
static int fails;

static void check(long got, long want, const char *name)
{
    n++;

    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
    }
}

/* What a port was told to be, for this run only */
static void told(const char *spelling)
{
    static char said[512];

    snprintf(said, sizeof said, "TOSEMU_MIDI=%s", spelling);
    putenv(said);
}

static void forget_the_setting(void)
{
    putenv((char *)"TOSEMU_MIDI=");
}

/* A file of bytes, for a port to read as though they had arrived */
static int put_file(const char *name, const unsigned char *bytes, int count)
{
    FILE *f = fopen(name, "wb");
    size_t put;

    if (!f)
        return 0;

    put = fwrite(bytes, 1, (size_t)count, f);
    fclose(f);

    return put == (size_t)count;
}

/* And back again, to see what a port wrote */
static int get_file(const char *name, unsigned char *into, int room)
{
    FILE *f = fopen(name, "rb");
    size_t got;

    if (!f)
        return -1;

    got = fread(into, 1, (size_t)room, f);
    fclose(f);

    return (int)got;
}

int main(void)
{
    unsigned char buffer[CAPACITY + 16];
    unsigned char note[3];
    uint8_t byte;
    int i;
    int got;
    int dropped;

    /*
     * A port nobody asked for.
     *
     * This is what the whole test suite runs as and what an ST with nothing in
     * the socket was, so it is first: everything below is the departure from
     * it. Sending to it works, because a byte with nowhere to go has not
     * failed to go there - a program that checked would stop on a machine with
     * no cable, which is not what a machine with no cable did.
     */
    forget_the_setting();

    check(midi_open(), 0, "a port nobody asked for is not opened");
    check(midi_wanted(), 0, "so there is no port");
    check(midi_fd(), -1, "and nothing to sleep on");
    check(midi_give(0x90), 1, "sending to it works");
    check(midi_take(&byte), 0, "and nothing ever arrives");
    midi_close();

    /*
     * A spelling nothing answers to, which is a person's mistake and has to be
     * said out loud rather than quietly becoming no MIDI at all.
     */
    told("wibble:1,2,3");
    check(midi_open(), 0, "a port spelled wrongly is refused");
    check(midi_wanted(), 0, "and leaves no port behind");
    midi_close();

    /* Out, through a file, which is the destination a test can look at */
    remove(SENT);
    told("file:," SENT);

    check(midi_open(), 1, "a file is a port");
    check(midi_wanted(), 1, "which is open");
    check(midi_fd(), -1,
          "and offers nothing to sleep on, a file always being ready");

    note[0] = 0x90;     /* note on, channel 1 */
    note[1] = 0x3c;     /* middle C */
    note[2] = 0x40;     /* half as hard as it goes */

    for (i = 0; i < 3; i++)
        check(midi_give(note[i]), 1, "a byte is taken for sending");

    /* Nothing has gone anywhere yet: the ring is what the machine writes into
     * and the pump is what empties it, and that is the whole of the split */
    check(get_file(SENT, buffer, sizeof buffer), 0,
          "what was written is still waiting to go");

    midi_pump();

    got = get_file(SENT, buffer, sizeof buffer);
    check(got, 3, "and after a pump all three bytes have gone");
    check(got == 3 && memcmp(buffer, note, 3) == 0, 1,
          "in the order they were written");

    midi_close();

    /* In, from a file, the same way round */
    note[0] = 0xf8;     /* a clock tick, which is the smallest thing there is */
    note[1] = 0x80;     /* note off */
    note[2] = 0x3c;

    check(put_file(ARRIVED, note, 3), 1, "a file of bytes to arrive");

    told("file:" ARRIVED);
    check(midi_open(), 1, "which is a port to read from");

    for (i = 0; i < 3; i++)
    {
        check(midi_take(&byte), 1, "a byte arrives");
        check(byte, note[i], "and is the one that was waiting");
    }

    check(midi_take(&byte), 0, "and then no more");
    midi_close();

    /* Both ways at once, which is what a program talking to a synthesiser does */
    remove(SENT);
    check(put_file(ARRIVED, note, 3), 1, "a file each way");
    told("file:" ARRIVED "," SENT);

    check(midi_open(), 1, "is one port");
    check(midi_take(&byte), 1, "with something waiting on it");
    check(midi_give(0xfe), 1, "and somewhere to send");
    midi_pump();
    check(get_file(SENT, buffer, sizeof buffer), 1, "which arrives");
    midi_close();

    /*
     * A far end that has stopped reading.
     *
     * The ring fills and then bytes are dropped, and dropping is the answer
     * rather than waiting: what is lost by dropping is a note, and what would
     * be lost by waiting is the sequencer that was playing it. The machine
     * must come back from a write to a port nobody is emptying.
     */
    told("file:");       /* a port with nowhere to send, so nothing drains it */
    check(midi_open(), 1, "a port with no destination is still a port");

    for (i = 0; i < CAPACITY; i++)
        if (!midi_give((uint8_t)(i & 0x7f)))
            break;

    check(i, CAPACITY, "the ring holds everything up to its capacity");
    check(midi_give(0x7f), 0, "and then says it is dropping what it is given");

    /* And it goes on saying so rather than quietly losing the rest */
    dropped = 0;
    for (i = 0; i < 16; i++)
        if (!midi_give(0x7f))
            dropped++;

    check(dropped, 16, "for every byte after that");

    /*
     * What is in the ring is still in the order it was written, which is the
     * part that matters: a ring that dropped from the front would have kept
     * the end of a message and thrown away its beginning, and the synthesiser
     * would be sent the tail of something it never heard the start of.
     */
    midi_reset();
    check(midi_give(0x11), 1, "after a reset there is room again");
    check(midi_give(0x22), 1, "for more than one");
    midi_close();

    /*
     * And a reset empties what was waiting to arrive as well. A program that
     * has gone took its half finished dump with it, and the next one must not
     * be handed the second half of it.
     */
    check(put_file(ARRIVED, note, 3), 1, "something waiting to arrive");
    told("file:" ARRIVED);
    check(midi_open(), 1, "on an open port");
    check(midi_take(&byte), 1, "of which one byte is read");
    midi_reset();
    check(midi_take(&byte), 0, "and a reset takes the rest away");
    midi_close();

    remove(SENT);
    remove(ARRIVED);

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
