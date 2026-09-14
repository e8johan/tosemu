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
 * The MIDI port, from the far side of the trap.
 *
 * There are two ways an ST program sends MIDI and they go to the same place:
 * Bconout on device 3, a byte at a time, which is what a program built out of
 * the BIOS does; and Midiws, which is the same thing with a count, and is what
 * anything sending a system exclusive dump uses because a dump is thousands of
 * bytes and a trap each would be thousands of traps.
 *
 * The count is why Midiws is worth a test of its own. It is the number of
 * bytes to send *less one* - so Midiws(0, p) sends one byte and Midiws(2, p)
 * sends three - which is the sort of thing that is read wrong once and then
 * drops the last note of every message for ever after. Off by one in the other
 * direction is worse: it sends a byte nobody put there.
 *
 * Run twice by the suite. Once with no port at all, which is what every other
 * test in this directory runs as and what an ST with nothing plugged into the
 * socket was: writing works and goes nowhere, which is not the same as
 * failing. Once with the bytes going to a file, where the Makefile compares
 * what came out against what should have. The argument says which.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>

#define DEV_PRT     (0)
#define DEV_AUX     (1)
#define DEV_CON     (2)
#define DEV_MIDI    (3)
#define DEV_IKBD    (4)

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

/* A call that has nothing to answer with, which is checked by coming back from
 * it at all: an unimplemented one halts the emulator instead */
static void survived(const char *name)
{
    n++;
    printf("ok %d - %s came back\n", n, name);
}

/*
 * A note on, a note off, and a short system exclusive between them.
 *
 * The sysex is there because it is the message with a length rather than a
 * shape: everything else on a MIDI cable is one, two or three bytes decided by
 * its first, and this one runs until 0xf7 says it has stopped. A port that
 * reassembles messages has to get it right and a port that does not has to
 * leave it alone, so it is the byte sequence most likely to come out changed.
 */
static unsigned char sysex[] = { 0xf0, 0x7d, 0x01, 0x02, 0x03, 0xf7 };

int main(int argc, char **argv)
{
    int sending = (argc > 1 && strcmp(argv[1], "send") == 0);
    int i;

    /*
     * Every device reports ready to send, including the ones that discard what
     * they are given. An application waiting for a port to drain would
     * otherwise spin for ever on one that is never going to answer - and that
     * is what bconout3 in TOS does before every single byte.
     */
    check(Bcostat(DEV_MIDI), -1, "MIDI reports ready to be written to");
    check(Bcostat(DEV_PRT), -1, "and so does the printer");
    check(Bcostat(DEV_AUX), -1, "and the serial port");

    /* One byte at a time, which is the BIOS way */
    Bconout(DEV_MIDI, 0x90);        /* note on, channel 1 */
    Bconout(DEV_MIDI, 0x3c);        /* middle C */
    Bconout(DEV_MIDI, 0x40);        /* half as hard as it goes */
    survived("Bconout to MIDI");

    /*
     * And a whole message at once. The count is one less than the number of
     * bytes, so this is all six of the sysex and not five of them.
     */
    Midiws(sizeof sysex - 1, (char *)sysex);
    survived("Midiws");

    /* A count of nought is one byte rather than none */
    Bconout(DEV_MIDI, 0x80);        /* note off */
    Bconout(DEV_MIDI, 0x3c);
    {
        unsigned char last = 0x40;

        Midiws(0, (char *)&last);
    }
    survived("Midiws with a count of nought");

    /*
     * The devices that still have nowhere to go. Writing to them has to work,
     * because a program that printed something and stopped on a machine with
     * no printer would be a program that could not be run at all.
     */
    Bconout(DEV_PRT, 'x');
    Bconout(DEV_AUX, 'x');
    Bconout(DEV_IKBD, 'x');
    survived("writing to the devices that are not there");

    if (argc > 1 && strcmp(argv[1], "receive") == 0)
    {
        /*
         * Bytes coming the other way, which on a real machine arrive in an
         * interrupt: the ACIA says a byte is there, the MFP raises the
         * channel, and what is on that channel puts the byte in the buffer
         * Iorec hands out. Nothing an application does makes any of that
         * happen, so what is checked is that the bytes are simply there.
         */
        _IOREC *rec = (_IOREC *)Iorec(2);
        long i;

        check(rec != 0L, 1, "there is an input record for MIDI");

        /* Bconstat is what a program polls, and it has to become true without
         * the program doing anything but ask */
        for (i = 0; i < 2000000L && !Bconstat(DEV_MIDI); i++)
            ;

        check(Bconstat(DEV_MIDI), -1, "and something arrives on it");

        /*
         * The bytes, in order. What was sent is the same three-byte note on
         * that goes the other way in the sending half of this file - the
         * Makefile writes the file it is read from.
         */
        check(Bconin(DEV_MIDI), 0x90, "a note on arrives");
        check(Bconin(DEV_MIDI), 0x3c, "then the note");
        check(Bconin(DEV_MIDI), 0x40, "then how hard it was struck");

        /*
         * And the record the application was handed is the one being filled.
         * A program watching a stream of notes reads this directly rather than
         * calling Bconin for each byte, which is what it was for.
         */
        check(rec->ibufsiz != 0, 1, "the record has a buffer with a size");
        check(rec->ibufhd == rec->ibuftl, 1,
              "and reads as empty once the bytes have been taken");

        printf("1..%d\n", n);
        return fails;
    }

    if (!sending)
    {
        /*
         * No port, so nothing can ever arrive. This is the half of the file
         * that runs in an ordinary make check, and it is the contract the rest
         * of the suite leans on: a machine with nothing in the socket.
         */
        check(Bconstat(DEV_MIDI), 0, "with no port, MIDI has nothing waiting");
        check(Bconin(DEV_MIDI), 0, "and reading it gives nothing rather than waiting");

        for (i = 0; i < 4; i++)
            check(Bconstat(DEV_MIDI), 0, "and goes on having nothing waiting");
    }

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
