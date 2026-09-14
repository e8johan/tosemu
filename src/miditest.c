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
 *
 * And the chips, for a stronger version of the same argument. A timer's period
 * and the MFP's idea of which channel wins are decisions, not behaviour: a
 * program can be written that provokes each of them, but what it observes is
 * the answer several layers away from where it was decided, so a wrong one
 * shows up as a sequencer running at the wrong tempo rather than as a divider
 * table with a hole in it. Asking the chip directly is both a sharper question
 * and one that needs no 68000 - which is what keeps this runnable on a build
 * server with no ALSA, no MIDI interface and no sound card at all.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "midi.h"
#include "mfp.h"
#include "acia.h"
#include "iorec.h"
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

/*
 * The ring an IOREC describes.
 *
 * Small on purpose: four slots means the wrap is three pushes away rather than
 * two hundred and fifty, and a test that has to get to the far end of a real
 * buffer to check the wrap is a test nobody reads the failure of.
 */
static void check_the_ring(void)
{
    uint8_t bytes[4];
    struct iorec_ring r;
    uint8_t got;
    int i;

    r.buf = bytes;
    r.size = (int)sizeof bytes;
    r.head = 0;
    r.tail = 0;

    check(iorec_count(&r), 0, "a new ring has nothing in it");
    check(iorec_capacity(&r), 3, "and holds one less than its size");
    check(iorec_take(&r, &got), 0, "taking from it says so");

    check(iorec_push(&r, 0x11), 1, "a byte goes in");
    check(iorec_count(&r), 1, "and is counted");

    /*
     * The byte lands at the slot *after* the old tail, which is the whole of
     * why this is not just any ring: an application walks these fields itself
     * and reads buf[head + 1], so a byte stored at the old index would be one
     * slot out of step from everything TOS ever wrote.
     */
    check(bytes[1], 0x11, "in the slot after the one tail named");
    check(r.tail, 1, "which is where tail now points");

    check(iorec_take(&r, &got), 1, "and comes back out");
    check(got, 0x11, "as itself");
    check(iorec_count(&r), 0, "leaving the ring empty");
    check(r.head, r.tail, "with head and tail the same, which is what empty is");

    /* Full, which is one short of the size */
    for (i = 0; i < 3; i++)
        check(iorec_push(&r, (uint8_t)(0x20 + i)), 1, "a byte goes into a filling ring");

    check(iorec_count(&r), 3, "three is as many as four slots hold");
    check(iorec_push(&r, 0xff), 0, "and the fourth is refused");
    check(iorec_count(&r), 3, "leaving what was there alone");

    /*
     * Refused rather than overwriting, and this is the half that matters. A
     * ring that dropped the oldest byte instead would keep the end of a
     * message and throw away its beginning, and what reached the far end would
     * be the tail of something it never heard the start of.
     */
    for (i = 0; i < 3; i++)
    {
        check(iorec_take(&r, &got), 1, "a byte comes out of a full ring");
        check(got, (uint8_t)(0x20 + i), "the oldest first, in order");
    }

    check(iorec_take(&r, &got), 0, "and then it is empty again");

    /* Round the end, which is where an index that was reduced rather than
     * compared would come out differently */
    for (i = 0; i < 6; i++)
    {
        check(iorec_push(&r, (uint8_t)(0x40 + i)), 1, "a byte goes in, going round");
        check(iorec_take(&r, &got), 1, "and comes out again");
        check(got, (uint8_t)(0x40 + i), "still itself after the wrap");
    }
}

/* The two 6850s */
static void check_the_acia(void)
{
    uint8_t byte;

    acia_reset();

    check(acia_read(ACIA_MIDI, 0) & ACIA_TDRE, ACIA_TDRE,
          "a fresh ACIA is ready to be sent to");
    check(acia_read(ACIA_MIDI, 0) & ACIA_RDRF, 0, "and has nothing to say");
    check(acia_interrupting(), 0, "and is not asking for attention");

    /* What TOS writes: receive interrupts on, divide by sixteen, eight bits */
    acia_write(ACIA_MIDI, 0, 0x95);
    check(acia_read(ACIA_MIDI, 0) & ACIA_IRQ, 0,
          "being told to interrupt is not the same as having something to say");

    acia_receive(ACIA_MIDI, 0x90);
    check(acia_read(ACIA_MIDI, 0) & ACIA_RDRF, ACIA_RDRF, "a byte arrives");
    check(acia_read(ACIA_MIDI, 0) & ACIA_IRQ, ACIA_IRQ, "and the chip says so");
    check(acia_interrupting(), 1, "which is what the MFP is watching for");

    check(acia_read(ACIA_MIDI, 1), 0x90, "the byte reads back");
    check(acia_read(ACIA_MIDI, 0) & ACIA_RDRF, 0, "and reading it clears the receiver");

    /*
     * And clears the interrupt with it, which is how TOS's handler gets out of
     * its loop: it reads until the chip stops asking, so a chip that went on
     * asking after its byte had been taken would never let it go.
     */
    check(acia_read(ACIA_MIDI, 0) & ACIA_IRQ, 0, "and stops it asking");
    check(acia_interrupting(), 0, "so nothing is interrupting any more");

    /* A byte on top of a byte */
    acia_receive(ACIA_MIDI, 0x3c);
    acia_receive(ACIA_MIDI, 0x40);
    check(acia_read(ACIA_MIDI, 0) & ACIA_OVRN, ACIA_OVRN,
          "a byte arriving on an unread one is an overrun");
    check(acia_read(ACIA_MIDI, 1), 0x40, "and the newer of the two is what is there");
    check(acia_read(ACIA_MIDI, 0) & ACIA_OVRN, 0, "reading clears the overrun as well");

    /* Told not to interrupt, it still receives */
    acia_write(ACIA_MIDI, 0, 0x15);     /* the same, without ACIA_RIE */
    acia_receive(ACIA_MIDI, 0x7f);
    check(acia_read(ACIA_MIDI, 0) & ACIA_RDRF, ACIA_RDRF,
          "a chip told not to interrupt still takes the byte");
    check(acia_read(ACIA_MIDI, 0) & ACIA_IRQ, 0, "and says nothing about it");
    check(acia_interrupting(), 0, "so the MFP hears nothing");

    /* A master reset throws away whatever was waiting */
    acia_write(ACIA_MIDI, 0, ACIA_RESET);
    check(acia_read(ACIA_MIDI, 0) & ACIA_RDRF, 0,
          "a master reset drops the byte that was waiting");

    /* Sending */
    acia_write(ACIA_MIDI, 0, 0x95);
    check(acia_take_transmitted(ACIA_MIDI, &byte), 0, "nothing has been sent yet");
    acia_write(ACIA_MIDI, 1, 0xf8);
    check(acia_take_transmitted(ACIA_MIDI, &byte), 1, "a byte written is a byte to send");
    check(byte, 0xf8, "and it is the one that was written");
    check(acia_take_transmitted(ACIA_MIDI, &byte), 0, "and only the once");

    /*
     * The keyboard chip, which is here because TOS's handler reads it on every
     * interrupt from either without asking which one interrupted. It must
     * answer, and it must answer that it has nothing to say.
     */
    check(acia_read(ACIA_IKBD, 0) & ACIA_RDRF, 0, "the keyboard ACIA never has a byte");
    check(acia_read(ACIA_IKBD, 0) & ACIA_TDRE, ACIA_TDRE, "and is always ready");
    check(acia_read(ACIA_IKBD, 0) & ACIA_IRQ, 0, "and never interrupts");

    /* Where each of them is in memory. The registers are two apart on the
     * upper half of the bus, so the odd addresses are not anything. */
    acia_reset();
    acia_write_at(4, 0x95);
    acia_receive(ACIA_MIDI, 0x42);
    check(acia_read_at(4) & ACIA_RDRF, ACIA_RDRF, "0xFFFC04 is the MIDI status");
    check(acia_read_at(6), 0x42, "and 0xFFFC06 is where its byte is");
    check(acia_read_at(0) & ACIA_RDRF, 0, "0xFFFC00 is the keyboard's, which is quiet");
    check(acia_read_at(5), 0, "and an odd address is nothing at all");
}

/* The 68901's idea of what should interrupt and in what order */
static void check_the_mfp(void)
{
    mfp_reset();

    check(mfp_pending_channel(), -1, "a fresh MFP has nothing to say");
    check(mfp_register(0x17), 0x48,
          "with vectors at 0x40 and the in-service bits kept, as TOS sets them");

    /*
     * Which register a channel lives in, which is the thing most easily read
     * backwards: the A registers hold the upper eight channels, so Timer A at
     * channel 13 is bit 5 of IERA while Timer C at channel 5 is bit 5 of IERB.
     */
    mfp_enable(MFP_TIMER_A);
    check(mfp_register(0x07), 1 << 5, "Timer A is bit five of IERA, being channel 13");
    check(mfp_register(0x09), 0, "and nothing is in IERB");

    mfp_enable(MFP_ACIA);
    check(mfp_register(0x09), 1 << 6, "the ACIA is bit six of IERB, being channel 6");

    mfp_enable(MFP_200HZ);
    check(mfp_register(0x09), (1 << 6) | (1 << 5),
          "and Timer C is bit five of the same one, being channel 5");

    /* A disabled channel hears nothing, and remembers nothing about it */
    mfp_disable(MFP_TIMER_B);
    mfp_raise(MFP_TIMER_B);
    check(mfp_pending_channel(), -1, "a channel that is off does not become pending");

    /* Priority: the higher number wins */
    mfp_raise(MFP_ACIA);
    check(mfp_pending_channel(), MFP_ACIA, "the one thing pending is the one that wins");
    mfp_raise(MFP_TIMER_A);
    check(mfp_pending_channel(), MFP_TIMER_A, "and 13 beats 6");

    /* The vector it goes through */
    check(mfp_vector_of(MFP_ACIA), 0x46, "the ACIA's vector is 0x46");
    check(mfp_vector_of(MFP_TIMER_A), 0x4d, "and Timer A's is 0x4D");

    /* Taking it clears the pending bit and sets the in-service one */
    check(mfp_acknowledge(), 0x4d, "taking the interrupt answers with its vector");
    check(mfp_register(0x0f), 1 << 5, "and Timer A is now in service");

    /*
     * And while it is, nothing below it gets a look in. That is what stops a
     * slow handler being interrupted by something less urgent and never
     * finishing - the ACIA is still pending, and still waiting.
     */
    check(mfp_pending_channel(), -1, "nothing below an in-service channel interrupts");
    check(mfp_register(0x0d), 1 << 6, "though the ACIA is still pending underneath");

    /*
     * The handler clears its own bit, and the write that does it is the one
     * that would go wrong: a nought clears and a one leaves alone, so 0xDF
     * means "clear bit five" rather than "set the other seven".
     */
    mfp_write_at(0x0f, (uint8_t)~(1 << 5));
    check(mfp_register(0x0f), 0, "a handler clears its own in-service bit");
    check(mfp_pending_channel(), MFP_ACIA, "and what was waiting underneath comes through");

    /*
     * And the same thing said as a call rather than as a register write, which
     * is what the channels tosemu answers for itself need: there is no handler
     * on them to write their bit away, so a channel acknowledged and never
     * finished holds itself off for ever. The first byte of MIDI arrives and
     * the second never does.
     */
    mfp_reset();
    mfp_enable(MFP_ACIA);
    mfp_raise(MFP_ACIA);
    check(mfp_acknowledge(), 0x46, "a channel nobody claimed is taken");
    check(mfp_pending_channel(), -1, "and holds itself off while it is in service");
    mfp_raise(MFP_ACIA);
    check(mfp_pending_channel(), -1, "even when it happens again");
    mfp_finished(MFP_ACIA);
    check(mfp_pending_channel(), MFP_ACIA,
          "and comes back the moment it is said to have finished");

    /* The same write, on the register TOS's ACIA handler actually writes */
    mfp_reset();
    mfp_enable(MFP_ACIA);
    mfp_enable(MFP_200HZ);
    mfp_raise(MFP_ACIA);
    mfp_raise(MFP_200HZ);
    check(mfp_register(0x0d), (1 << 6) | (1 << 5), "two channels pending in IPRB");
    mfp_write_at(0x0d, 0xbf);
    check(mfp_register(0x0d), 1 << 5,
          "writing 0xBF clears bit six and leaves the rest, as TOS does");

    /* Masked off is pending but not asking, and stays pending */
    mfp_reset();
    mfp_enable(MFP_TIMER_A);
    mfp_write_at(0x13, 0);              /* IMRA, everything masked */
    mfp_raise(MFP_TIMER_A);
    check(mfp_pending_channel(), -1, "a masked channel does not interrupt");
    check(mfp_register(0x0b), 1 << 5, "but it is pending, and has not lost what happened");
    mfp_write_at(0x13, 1 << 5);
    check(mfp_pending_channel(), MFP_TIMER_A, "so unmasking it lets it through at once");

    /* Turning a channel off takes its pending bit with it */
    mfp_reset();
    mfp_enable(MFP_TIMER_A);
    mfp_raise(MFP_TIMER_A);
    check(mfp_register(0x0b), 1 << 5, "a pending channel");
    mfp_write_at(0x07, 0);              /* IERA */
    check(mfp_register(0x0b), 0, "turned off, is no longer pending either");

    /* Without the software end of interrupt there is nothing to clear */
    mfp_reset();
    mfp_write_at(0x17, 0x40);           /* vectors at 0x40, bit 3 clear */
    mfp_enable(MFP_TIMER_A);
    mfp_raise(MFP_TIMER_A);
    check(mfp_acknowledge(), 0x4d, "the vector is the same either way");
    check(mfp_register(0x0f), 0,
          "but with no software end of interrupt nothing goes in service");

    /*
     * The ACIAs' line into the chip, which is active low and is the one TOS
     * spins on: its handler loops while the bit reads nought, so a bit that
     * never went back up would leave it going round for ever.
     */
    mfp_reset();
    check(mfp_register(0x01) & (1 << MFP_GPIP_ACIA), 1 << MFP_GPIP_ACIA,
          "with no ACIA interrupting the line reads high");

    mfp_enable(MFP_ACIA);
    mfp_write_at(0x03, 0);              /* AER: the falling edge is the one */
    mfp_gpip(MFP_GPIP_ACIA, 0);
    check(mfp_register(0x01) & (1 << MFP_GPIP_ACIA), 0, "a chip asking pulls it low");
    check(mfp_pending_channel(), MFP_ACIA, "and that edge is what raises channel 6");

    mfp_gpip(MFP_GPIP_ACIA, 1);
    check(mfp_register(0x01) & (1 << MFP_GPIP_ACIA), 1 << MFP_GPIP_ACIA,
          "and letting go puts it back up, which is how TOS's handler ends");

    /* The machine cannot write the input pins */
    mfp_write_at(0x01, 0x00);
    check(mfp_register(0x01) & (1 << MFP_GPIP_ACIA), 1 << MFP_GPIP_ACIA,
          "what the machine writes to the input pins goes nowhere");
}

/* How long a timer waits, which is the arithmetic a tempo is made of */
static void check_the_timers(void)
{
    mfp_reset();

    /*
     * EmuTOS's own system clock: divide by sixty four, count a hundred and
     * ninety two. That is 200Hz exactly, and it is the one value in this file
     * that can be checked against something other than this file.
     */
    mfp_setup_timer(2, 0x50, 192);
    check(mfp_timer_period(2), 5000000L,
          "Timer C set as EmuTOS sets it is five milliseconds, which is 200Hz");
    check(mfp_timer_channel(2), MFP_200HZ, "and it interrupts on channel 5");

    /*
     * Timers C and D share a register, C in the upper half. Setting one must
     * leave the other alone, the other being very likely to be the system
     * clock.
     */
    mfp_setup_timer(3, 0x07, 192);
    check(mfp_timer_period(3), 15625000L, "Timer D divides by two hundred");
    check(mfp_timer_period(2), 5000000L, "and setting it left Timer C alone");

    /* The whole divider table, on timer A where the field is its own register.
     * A count of 192 makes every one of them come out exact. */
    mfp_setup_timer(0, 1, 192);
    check(mfp_timer_period(0), 312500L, "divide by four");
    mfp_setup_timer(0, 2, 192);
    check(mfp_timer_period(0), 781250L, "by ten");
    mfp_setup_timer(0, 3, 192);
    check(mfp_timer_period(0), 1250000L, "by sixteen");
    mfp_setup_timer(0, 4, 192);
    check(mfp_timer_period(0), 3906250L, "by fifty");
    mfp_setup_timer(0, 5, 192);
    check(mfp_timer_period(0), 5000000L, "by sixty four");
    mfp_setup_timer(0, 6, 192);
    check(mfp_timer_period(0), 7812500L, "by a hundred");
    mfp_setup_timer(0, 7, 192);
    check(mfp_timer_period(0), 15625000L, "by two hundred");

    /* Nought is stopped, and not a divider of one */
    mfp_setup_timer(0, 0, 192);
    check(mfp_timer_period(0), 0L, "a control of nought is a timer that is not running");

    /*
     * A data register of nought counts the whole way round. This is the one
     * that reads as an obvious special case and is not: the counter is
     * decremented and tested, so zero goes all the way to 256.
     */
    mfp_setup_timer(0, 5, 0);
    check(mfp_timer_period(0), 6666667L,
          "a count of nought is two hundred and fifty six, rounded to the nearest");

    /*
     * The upper modes count edges on a pin and measure pulses, neither of
     * which is a length of time. Refused out loud rather than timed as though
     * they were delays - a Timer B counting display lines that quietly never
     * fired would look like the emulator having lost the interrupt.
     */
    mfp_setup_timer(0, 8, 192);
    check(mfp_timer_period(0), -1L, "counting edges is not a delay and says so");
    mfp_setup_timer(0, 15, 192);
    check(mfp_timer_period(0), -1L, "and neither is measuring a pulse");

    /* Which channel each of the four interrupts on */
    check(mfp_timer_channel(0), MFP_TIMER_A, "Timer A is channel 13");
    check(mfp_timer_channel(1), MFP_TIMER_B, "Timer B is channel 8");
    check(mfp_timer_channel(3), MFP_TIMER_D, "Timer D is channel 4");
    check(mfp_timer_channel(4), -1, "and there is no fifth timer");
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

    check_the_ring();
    check_the_acia();
    check_the_mfp();
    check_the_timers();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
