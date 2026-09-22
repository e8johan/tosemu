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
 * The chips at the top of the memory map, from a program's side of them.
 *
 * bin/miditest checks what the MFP decides - which channel wins, how long a
 * timer waits - by asking it directly, with no machine involved. What it
 * cannot check is that any of it is reachable: that 0xFFFA00 is somewhere a
 * program can read at all, that the clock is running before the program
 * started rather than from when it first looked, and that what TOS would have
 * left set up is set up.
 *
 * That is what this is for: the machine having the chips, the timers running,
 * the counters counting, and nothing left stuck afterwards. What a handler the
 * application installs does when it is called is tests/c-timer.c's question,
 * and what happens while the application waits for GEM is tests/c-clock.c's.
 *
 * Run two ways by the suite. With interrupts asked for, which is everything
 * below; and without, which is every other test in this directory, where none
 * of it is there at all and reading 0xFFFA00 would stop the emulator. The
 * argument says which.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>

/* The MFP, and the registers of it this asks about */
#define MFP_BASE  (0xFFFA00L)
#define MFP_GPIP  (MFP_BASE + 0x01)
#define MFP_IERB  (MFP_BASE + 0x09)
#define MFP_IPRB  (MFP_BASE + 0x0D)
#define MFP_ISRB  (MFP_BASE + 0x11)
#define MFP_VR    (MFP_BASE + 0x17)
#define MFP_TCDCR (MFP_BASE + 0x1D)
#define MFP_TCDR  (MFP_BASE + 0x23)

/* And the MIDI ACIA */
#define ACIA_MIDI_STATUS (0xFFFC04L)

/* The two hundred hertz counter, which lives where a program in user mode
 * cannot reach it */
#define HZ200 (0x4baL)

/* And the count of frames the video hardware has drawn */
#define FRCLOCK (0x466L)

#define TIMER_C_CHANNEL (5)

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

static unsigned char peek(long address)
{
    return *(volatile unsigned char *)address;
}

/*
 * What the clock did while we watched it.
 *
 * Gathered inside Supexec and reported afterwards, the way c-super.c does and
 * for the same reason: the system variables are out of reach in user mode, and
 * printf from inside supervisor mode is a great deal more stack than the few
 * words a routine run that way should be spending.
 */
static unsigned long clock_before;
static unsigned long clock_after;
static long clock_moved_by;

static void watch_the_clock(void)
{
    volatile unsigned long *counter = (volatile unsigned long *)HZ200;
    long spins;

    clock_before = *counter;

    /*
     * Spun rather than slept, there being nothing to sleep on: a program of
     * the period waiting for the clock did exactly this. Bounded so that a
     * clock which is not running fails the check rather than hanging the suite
     * - a test that stops says nothing about what it found.
     */
    for (spins = 0; spins < 20000000L; spins++)
    {
        clock_after = *counter;

        if (clock_after != clock_before)
            break;
    }

    clock_moved_by = (long)(clock_after - clock_before);
}

/*
 * And the frames, on a machine that does not interrupt: they are the video
 * hardware's rather than the MFP's, so they go on being counted when nothing
 * else is. Spun until two have gone by, which is long enough for the two
 * hundred hertz counter to have moved eight times if it were running.
 */
static unsigned long frames_before, frames_after;
static unsigned long ticks_before, ticks_after;

static void watch_the_frames(void)
{
    volatile unsigned long *frames = (volatile unsigned long *)FRCLOCK;
    volatile unsigned long *ticks = (volatile unsigned long *)HZ200;
    long spins;

    frames_before = *frames;
    ticks_before = *ticks;

    for (spins = 0; spins < 5000000L; spins++)
        if (*frames - frames_before >= 2)
            break;

    frames_after = *frames;
    ticks_after = *ticks;
}

int main(int argc, char **argv)
{
    int with_interrupts = (argc > 1 && strcmp(argv[1], "on") == 0);

    if (!with_interrupts)
    {
        /*
         * The ordinary machine, which is what the rest of the suite runs on.
         *
         * The chips are here even so, because they were on the machine: an ST
         * has an MFP and two ACIAs at these addresses whether or not anything
         * intends to use them, and a program that reads one without having
         * announced an interest in interrupts must get an answer rather than
         * stop the emulator. That is what this half is for - reading 0xFFFA00
         * used to be the thing this file had to avoid, and is now the thing it
         * is checking.
         */
        check(peek(MFP_VR), 0x48, "the MFP is there on an ordinary machine too");
        check(peek(ACIA_MIDI_STATUS) & 0x02, 0x02,
              "and so is the MIDI ACIA, ready to be sent to");

        /*
         * And nothing is running behind them. Timer C is the one TOS leaves
         * going, so its channel being disabled is what says this machine has
         * the chips without the clock - which is the whole difference between
         * the two halves of this test.
         */
        check((peek(MFP_IERB) >> TIMER_C_CHANNEL) & 1, 0,
              "but nothing is enabled, there being no clock behind them");

        Supexec(watch_the_frames);
        check(frames_after - frames_before >= 2, 1,
              "the video hardware's frames are counted all the same");
        check((long)(ticks_after - ticks_before), 0,
              "and the MFP's clock does not move");

        printf("1..%d\n", n);
        return fails;
    }

    /*
     * The vector register, which says where the sixteen channels' vectors
     * start and whether the chip keeps track of what is being serviced. TOS
     * sets 0x48 - vectors at 0x40, and the in-service bits kept - and a
     * program that never writes this one expects to find that.
     */
    check(peek(MFP_VR), 0x48, "the MFP is there, set up the way TOS left it");

    /*
     * The system timer, which was running before this program started. Timer C
     * at two hundred hertz is divide by sixty four - a five in the upper half
     * of the register it shares with Timer D - counting a hundred and ninety
     * two.
     */
    check((peek(MFP_TCDCR) >> 4) & 7, 5, "Timer C divides the clock by sixty four");
    check(peek(MFP_TCDR), 192, "and counts a hundred and ninety two of them");
    check((peek(MFP_IERB) >> TIMER_C_CHANNEL) & 1, 1,
          "and its channel is enabled, as TOS enabled it");

    /* The ACIAs, which have to be readable whether or not anything is plugged
     * in: TOS's own interrupt handler reads both on every interrupt */
    check(peek(ACIA_MIDI_STATUS) & 0x02, 0x02, "the MIDI ACIA is ready to be sent to");
    check(peek(ACIA_MIDI_STATUS) & 0x01, 0, "and has nothing waiting to be read");
    check((peek(MFP_GPIP) >> 4) & 1, 1,
          "and is not pulling the MFP's fourth input down");

    /*
     * And the clock, which is the one thing here that is not a register
     * somebody set: it moves because time passes, and it was moving before
     * this program was loaded.
     */
    Supexec(watch_the_clock);

    check(clock_moved_by > 0, 1, "the two hundred hertz counter is counting");

    /*
     * One tick and not a burst of them. A counter that jumped by hundreds
     * would be one being caught up from whenever the machine last looked
     * rather than one keeping time.
     */
    check(clock_moved_by < 50, 1, "and it counts at something like the right rate");

    /*
     * And nothing is stuck.
     *
     * Timer C has been going off all along and the machine has been taking
     * each one - there is no handler on its vector, so tosemu answers for it
     * and says it has finished. What that has to leave behind is a channel
     * that is not in service, because a channel left in service holds off
     * itself and every lower one for ever. It is the failure that looks like
     * the device having gone quiet rather than like anything being wrong, and
     * it is what stopped the second byte of MIDI ever arriving.
     */
    check((peek(MFP_ISRB) >> TIMER_C_CHANNEL) & 1, 0,
          "and the interrupts it took were finished with rather than left open");

    /*
     * The same for the ACIA's channel, which shares the register. Nothing has
     * arrived on it - there is no port - but the machine set it up, and a
     * channel that was never in service must not read as though it were.
     */
    check((peek(MFP_ISRB) >> 6) & 1, 0, "and the same for the MIDI channel");

    printf("1..%d\n", n);

    return fails;
}
