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
 * A handler the application installed, and whether the machine ever calls it.
 *
 * This is the thing a sequencer is built out of. Xbtimer sets one of the MFP's
 * timers going and hangs a routine off its vector, and from then on the routine
 * is what happens - the tempo, the notes going out, all of it - while the main
 * program gets on with being a GEM application. Nothing about that works
 * unless an interrupt actually interrupts.
 *
 * Timer A rather than the system clock, because Timer A is the one a sequencer
 * uses: Timer C belongs to the machine and is already running something. A
 * hundred hertz is slow enough to be certain it is the timer being counted
 * rather than the emulator racing, and fast enough that the test does not take
 * long.
 *
 * The handler is not an ordinary function and cannot be written as one. It
 * has to end in RTE rather than RTS and it has to save whatever it touches,
 * and the compiler's own attribute for saying so is the way to get that
 * right - a plain function with an RTE put in the middle of it returns through
 * a stack frame the compiler pushed and the RTE knows nothing about, which
 * takes the machine somewhere neither of them meant.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>

/* Timer A, and the channel it interrupts on */
#define TIMER_A         (0)
#define TIMER_A_CHANNEL (13)
#define TIMER_A_VECTOR  (0x100L + 4 * TIMER_A_CHANNEL)

/*
 * Divide the two and a half megahertz timer clock by two hundred and count a
 * hundred and twenty three of them, which is a hundred hertz near enough.
 * Control 7 is the divide by two hundred.
 */
#define TIMER_A_CONTROL (7)
#define TIMER_A_DATA    (123)

#define HZ200 (0x4baL)

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

/* What the handler counts. Volatile because nothing the compiler can see ever
 * writes it - the only thing that does is an interrupt. */
static volatile long ticks;

/*
 * The handler, which the compiler is told is one: it saves what it touches and
 * comes back with RTE rather than RTS.
 *
 * Clearing its own in-service bit is not tidiness, it is the arrangement. The
 * MFP is set up to keep track of what is being serviced - that is what bit
 * three of its vector register asks for, and TOS sets it - and a channel that
 * is in service holds off itself and everything below it until somebody says
 * it is finished. A handler that returns without clearing its bit is called
 * exactly once and never again.
 *
 * A nought clears and a one leaves alone, so 0xDF means bit five of ISRA,
 * which is channel thirteen. TOS's own Timer C handler ends with the same
 * write to the other register - see vectors.S in EmuTOS.
 */
#define MFP_ISRA (0xFFFA0FL)

static void __attribute__((interrupt_handler)) timer_handler(void)
{
    ticks++;

    *(volatile unsigned char *)MFP_ISRA = (unsigned char)~(1 << 5);
}

/* Reading the clock, which lives where a program in user mode cannot reach */
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

/* What the vector held before anything was installed */
static long vector_before;

static void read_the_vector(void)
{
    vector_before = *(volatile long *)TIMER_A_VECTOR;
}

int main(int argc, char **argv)
{
    unsigned long started;
    long seen;
    long spins;

    Supexec(read_the_vector);
    check(vector_before, 0, "nothing is on Timer A's vector to begin with");

    /*
     * Set the timer going and hang the handler off it, which is the one call
     * that does both. Everything a sequencer does to start its clock is this
     * line.
     */
    Xbtimer(TIMER_A, TIMER_A_CONTROL, TIMER_A_DATA, timer_handler);

    Supexec(read_the_vector);
    check(vector_before == (long)timer_handler, 1,
          "Xbtimer puts the handler on the vector");

    /*
     * And then wait, doing nothing in particular, the way a program does
     * between one thing and the next. The counter is bumped by something that
     * is not this loop.
     *
     * Bounded by the machine's own clock rather than by a count of spins, so
     * that this waits for a length of time rather than for an amount of work:
     * a hundred hertz for a fifth of a second is twenty, and forty ticks of
     * the two hundred hertz counter is that fifth of a second.
     */
    started = clock_reading();

    for (spins = 0; spins < 200000000L; spins++)
    {
        if (clock_reading() - started >= 40)
            break;
    }

    seen = ticks;

    printf("# Timer A ran for about a fifth of a second and fired %ld times\n",
           seen);

    check(seen > 0, 1, "a handler on a timer's vector is called");

    /*
     * About the right number of times. A hundred hertz for a fifth of a second
     * is twenty; the bounds are wide because the host is a machine with other
     * things on it, and what is being checked is that this is a timer rather
     * than something firing as fast as it can.
     */
    check(seen >= 5, 1, "at something like the rate it was set to");
    check(seen < 200, 1, "and not as fast as the machine can go");

    /* Put it back, so that nothing is left running after this returns */
    Xbtimer(TIMER_A, 0, 0, 0L);

    printf("1..%d\n", n);

    return fails;
}
