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
 * Whether the machine's clock keeps running while the application is waiting
 * for GEM.
 *
 * It is the same clock either way round, but the two halves of the emulator
 * are not. The timers are driven from the instruction hook, which runs before
 * each instruction the machine executes - and while an application sits in
 * evnt_multi no instruction executes at all, the emulator being asleep in
 * poll. So the wait has to know when the next thing is due and stop sleeping
 * for it.
 *
 * What it looks like when it does not is the interesting part, and it is not a
 * clock that stops. The moment the application gets an event and starts
 * executing again, the timers notice how long they have been owed and catch
 * up - so a short wait comes out right, in a burst, and only a long one shows
 * anything. There is a limit on how much may be caught up in one go, because a
 * laptop that was closed for an hour would otherwise owe millions of
 * interrupts, and past that limit what was missed is dropped. That is the hole
 * this measures: wait for longer than the limit covers and the clock comes
 * back short.
 *
 * For a sequencer, the burst is the bug rather than the shortfall. Its tempo
 * is a timer, and an application that waits for GEM between notes would play
 * them in clumps whenever somebody stopped moving the mouse.
 *
 * Two thirds of a second, which is twice what the catch-up limit covers at two
 * hundred hertz. Nothing is injected and there is no compositor: the timer on
 * the wait is the only thing that can end it, so a wait that blocks stops the
 * emulator and the count line at the end goes missing.
 */

#include <stdio.h>
#include <gem.h>
#include <mint/osbind.h>

#define HZ200 (0x4baL)

/* Timer A, which is the one a sequencer uses, and the register its handler
 * has to clear its own in-service bit in - see tests/c-timer.c */
#define TIMER_A         (0)
#define TIMER_A_CONTROL (7)     /* divide the timer clock by two hundred */
#define TIMER_A_DATA    (123)   /* and count this many, which is 100Hz */
#define MFP_ISRA        (0xFFFA0FL)
#define TIMER_A_ISR_BIT (5)

/* How long to wait, and how many two hundred hertz ticks that is */
#define WAIT_MS   (660)
#define WAIT_TICKS (WAIT_MS / 5)

/*
 * What to insist on. Well under the ticks the wait is worth, because this is
 * not a test of how accurate the clock is - the host is a machine running
 * other things - and well over what the catch-up limit alone can deliver,
 * which is sixty four.
 */
#define AT_LEAST (100)

/*
 * A handler of the application's own, and the whole point of the second half
 * of this file: it has to be called while the application is asleep in GEM.
 *
 * That is the case a sequencer lives in. Its tempo is a timer and its main
 * loop is a GEM event loop, so every note it plays is played by a handler
 * firing at a moment when the application itself is doing nothing at all. A
 * machine that only takes interrupts between the instructions of a running
 * program would play nothing until somebody moved the mouse.
 */
static volatile long fired;

static void __attribute__((interrupt_handler)) timer_handler(void)
{
    fired++;

    *(volatile unsigned char *)MFP_ISRA = (unsigned char)~(1 << TIMER_A_ISR_BIT);
}

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

/* Read inside Supexec and reported afterwards: the counter is in the area a
 * program in user mode cannot reach, and printf from supervisor mode is a
 * great deal more stack than a routine run that way should spend */
static unsigned long clock_now;

static void read_the_clock(void)
{
    clock_now = *(volatile unsigned long *)HZ200;
}

static unsigned long clock_reading(void)
{
    Supexec(read_the_clock);

    return clock_now;
}

int main(int argc, char **argv)
{
    unsigned long before, after;
    long moved;
    short message[8];
    short mx = 0, my = 0, mb = 0, ks = 0, kr = 0, br = 0;

    appl_init();

    before = clock_reading();

    evnt_multi(MU_TIMER | MU_MESAG, 0, 0, 0,
               0, 0, 0, 0, 0,
               0, 0, 0, 0, 0,
               message, WAIT_MS,
               &mx, &my, &mb, &ks, &kr, &br);

    after = clock_reading();
    moved = (long)(after - before);

    printf("# waited %d ms, which is %d ticks; the clock moved %ld\n",
           WAIT_MS, WAIT_TICKS, moved);

    check(moved >= AT_LEAST, 1,
          "the clock keeps running while the application waits for GEM");

    /*
     * And has not run away with itself. A counter that moved by thousands
     * would be one being driven by something other than the passing of time,
     * which is as wrong as one that stopped and rather harder to notice.
     */
    check(moved < WAIT_TICKS * 2, 1, "and at something like the right rate");

    /*
     * And again with a handler of the application's own on a timer, which is
     * the thing this is really about. The wait is the same wait; what is being
     * asked is whether an interrupt can be taken at all while the emulator is
     * asleep in poll with no instruction stream to interrupt.
     */
    fired = 0;
    Xbtimer(TIMER_A, TIMER_A_CONTROL, TIMER_A_DATA, timer_handler);

    evnt_multi(MU_TIMER | MU_MESAG, 0, 0, 0,
               0, 0, 0, 0, 0,
               0, 0, 0, 0, 0,
               message, WAIT_MS,
               &mx, &my, &mb, &ks, &kr, &br);

    Xbtimer(TIMER_A, 0, 0, 0L);

    printf("# a hundred hertz timer fired %ld times during the wait\n",
           (long)fired);

    check(fired > 0, 1,
          "a handler of the application's own is called while it waits for GEM");
    check(fired >= 20, 1, "and goes on being called for as long as the wait");

    appl_exit();

    printf("1..%d\n", n);

    return 0;
}
