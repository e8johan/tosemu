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

/* See interrupt.h for what this is and what is deliberately not in it. */

#include "interrupt.h"

#include <stdio.h>
#include <time.h>

#include "mfp.h"
#include "acia.h"
#include "midi.h"
#include "memory.h"
#include "settings.h"
#include "tossystem.h"
#include "m68k.h"

/*
 * How many instructions go by between looks at the clock.
 *
 * The instruction hook runs before every instruction the machine executes, and
 * a clock read there would be the most expensive thing in the emulator. So
 * almost every call is a decrement and a test, and the work happens on one in
 * however many this is.
 *
 * A few thousand is tens of microseconds at the speed the host runs Musashi,
 * which is well inside the shortest interval a sequencer asks Timer A for.
 * Much smaller costs the whole emulator for no accuracy anybody can hear; much
 * larger and a fast timer starts arriving in bursts.
 */
#define TICK_INSTRUCTIONS (4096)

/*
 * The most times one timer may fire to catch up in a single look at the clock.
 *
 * A laptop that was closed for an hour comes back with a Timer A that owes
 * eighteen million interrupts, and paying that back would mean the machine
 * never executing another instruction. Past this the timer is simply moved to
 * now, having lost what it missed - which is what a real machine that was
 * switched off would also have done.
 */
#define CATCHUP_MOST (64)

/* The system variables this keeps. They are LONGs, big endian, in the area
 * from 0x380 up that a program in user mode cannot reach. */
#define SYSVAR_VBCLOCK (0x462) /* Vertical blanks since the machine started */
#define SYSVAR_FRCLOCK (0x466) /* And the same, not counting the ones missed */
#define SYSVAR_HZ200   (0x4BA) /* Two hundred hertz since the machine started */

/* The vertical blank, which is not an MFP channel at all - it is a level four
 * autovectored interrupt straight from the video hardware. Fifty a second on
 * every one of these machines, which is what Tickcal reports as 20ms. */
#define VBL_PERIOD_NS (20000000L)

struct source {
    int       channel;    /* An MFP channel, or -1 for the vertical blank */
    long      period_ns;  /* 0 when it is not running */
    long long due_ns;
};

/* The four MFP timers and the vertical blank */
#define SOURCES (MFP_TIMER_COUNT + 1)
#define VBL_SOURCE (MFP_TIMER_COUNT)

static struct source sources[SOURCES];

static int wanted;
static int settled;
static int built;
static int countdown;

static long long hz200_count;
static long long vbl_count;

static long long now_ns(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);

    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* Said once each, however often the thing that prompted it happens */
static void said(const char *what)
{
    static const char *before[8];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (before[i] == what)
            return;

    if (count < (int)(sizeof before / sizeof before[0]))
        before[count++] = what;

    printf("tosemu: %s\n", what);
    fflush(stdout);
}

int interrupt_wanted(void)
{
    if (!settled)
    {
        settled = 1;

        /*
         * Asked for outright, or implied by there being a MIDI port. A program
         * given something to play notes at is a program that will be setting
         * timers going and hanging handlers off them, and it has to be started
         * on a machine where that works.
         */
        wanted = setting_flag("TOSEMU_INTERRUPTS") || midi_asked_for();
    }

    return wanted;
}

/* The system variables ******************************************************/

/*
 * A long into the machine's low memory, written around the emulated
 * processor's idea of what it is allowed to touch.
 *
 * The obvious way is m68k_write_memory_32, and it is wrong here. That goes
 * through tos_write, which checks the mode the *emulated* CPU is in, and the
 * area these live in is readable and writeable in supervisor mode only. So
 * bumping a counter while the application happened to be in user mode - which
 * is most of the time - would be refused, and the refusal calls
 * halt_execution: the emulator would stop at a random instruction with a
 * message about memory that is not writeable.
 *
 * tos_mem_to_host_mem hands back a pointer into the block instead, which is
 * the same memory without the question being asked. The bytes go in big endian
 * by hand because what reads them is a 68000.
 */
static void poke_system_long(uint32_t address, uint32_t value)
{
    uint8_t *at = tos_mem_to_host_mem(address);

    if (!at)
        return;

    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

/* The chips, as memory ******************************************************/

static uint8_t mfp_area_read(struct _memarea *area, uint32_t address)
{
    (void)area;

    return mfp_read_at(address - MFP_BASE_ADDRESS);
}

static void mfp_area_write(struct _memarea *area, uint32_t address,
                           uint8_t value)
{
    (void)area;

    mfp_write_at(address - MFP_BASE_ADDRESS, value);

    /*
     * A control register may have started or stopped a timer, and what was
     * worked out about how long each one runs for is no longer true. Asked
     * about every write rather than only the ones that could matter, because
     * recomputing four dividers is cheaper than deciding whether to.
     */
    interrupt_timers_changed();
}

/* What the two ACIAs and the MFP have to say to each other, which is one wire:
 * both chips' interrupt lines are tied together into the MFP's fourth general
 * purpose input, and it is active low. */
static void acia_settled(void)
{
    mfp_gpip(MFP_GPIP_ACIA, !acia_interrupting());
}

static uint8_t acia_area_read(struct _memarea *area, uint32_t address)
{
    uint8_t value;

    (void)area;

    value = acia_read_at(address - ACIA_BASE_ADDRESS);

    /*
     * Reading the data register is what stops a chip asking, so the wire into
     * the MFP has to be looked at again afterwards. This is the half that TOS's
     * own handler depends on: it loops while the input reads low, so an input
     * that did not go back up when the byte was taken would leave it going
     * round for ever.
     */
    acia_settled();

    return value;
}

static void acia_area_write(struct _memarea *area, uint32_t address,
                            uint8_t value)
{
    (void)area;

    acia_write_at(address - ACIA_BASE_ADDRESS, value);

    /* Writing the control register can turn the receive interrupt on, which
     * can start it asking about a byte that was already waiting */
    acia_settled();
}

/* Building it ***************************************************************/

void interrupt_init(void)
{
    int i;

    if (!interrupt_wanted())
        return;

    mfp_reset();
    acia_reset();

    /*
     * Readable and writeable in both modes, which is a departure and a
     * deliberate one. An ST bus errors these in user mode, but tos_read does
     * not raise a bus error - it calls halt_execution. So refusing a user mode
     * poke would turn a program that touches the MFP outside Supexec into a
     * dead emulator, which is a worse answer than letting it read a register.
     */
    add_fnct_memory_area("mfp",
                         MEMORY_READWRITE | MEMORY_SUPERREAD | MEMORY_SUPERWRITE,
                         MFP_BASE_ADDRESS, MFP_LENGTH, 0,
                         mfp_area_read, mfp_area_write);

    add_fnct_memory_area("acia",
                         MEMORY_READWRITE | MEMORY_SUPERREAD | MEMORY_SUPERWRITE,
                         ACIA_BASE_ADDRESS, ACIA_LENGTH, 0,
                         acia_area_read, acia_area_write);

    for (i = 0; i < SOURCES; i++)
    {
        sources[i].channel = (i == VBL_SOURCE) ? -1 : mfp_timer_channel(i);
        sources[i].period_ns = 0;
        sources[i].due_ns = 0;
    }

    /*
     * The system timer, set up the way TOS leaves it: divide the timer clock by
     * sixty four and count a hundred and ninety two, which is two hundred hertz
     * exactly. Channel five enabled, because TOS enabled it.
     *
     * This is the machine's own clock rather than the program's. A program
     * reads 0x4BA expecting it to have been counting since the machine was
     * switched on, and on an ST it had been - so a machine that only started
     * counting when somebody asked would be one where every measured interval
     * came out wrong. A program that wants the clock faster reprograms it, and
     * then this is what it is reprogramming.
     */
    mfp_setup_timer(2, 0x50, 192);
    mfp_enable(MFP_200HZ);

    hz200_count = 0;
    vbl_count = 0;
    built = 1;
    countdown = 0;

    interrupt_timers_changed();

    sources[VBL_SOURCE].period_ns = VBL_PERIOD_NS;
    sources[VBL_SOURCE].due_ns = now_ns() + VBL_PERIOD_NS;
}

void interrupt_reset(void)
{
    if (!built)
        return;

    mfp_reset();
    acia_reset();

    mfp_setup_timer(2, 0x50, 192);
    mfp_enable(MFP_200HZ);

    interrupt_timers_changed();
}

void interrupt_timers_changed(void)
{
    long long now;
    int i;

    if (!built)
        return;

    now = now_ns();

    for (i = 0; i < MFP_TIMER_COUNT; i++)
    {
        long period = mfp_timer_period(i);

        /*
         * A mode that counts rather than waits. Said out loud rather than
         * quietly never firing, because a Timer B set to count display lines
         * that simply never went off looks exactly like the emulator having
         * lost an interrupt.
         */
        if (period < 0)
        {
            said("MFP, a timer was set to count edges rather than to wait, "
                 "which this does not have anything to count");
            period = 0;
        }

        if (period == 0)
        {
            sources[i].period_ns = 0;
            continue;
        }

        /* A timer that was already running at this rate keeps its place in the
         * cycle. Restarting it on every write to an unrelated register would
         * stop a fast timer ever reaching the end of its count. */
        if (sources[i].period_ns != period)
        {
            sources[i].period_ns = period;
            sources[i].due_ns = now + period;
        }
    }
}

/* Running it ****************************************************************/

int interrupt_fd(void)
{
    if (!built)
        return -1;

    return midi_fd();
}

long interrupt_next_due_ms(void)
{
    long long now;
    long long soonest = -1;
    int i;

    if (!built)
        return -1;

    now = now_ns();

    for (i = 0; i < SOURCES; i++)
    {
        if (sources[i].period_ns == 0)
            continue;

        if (soonest < 0 || sources[i].due_ns < soonest)
            soonest = sources[i].due_ns;
    }

    if (soonest < 0)
        return -1;

    if (soonest <= now)
        return 0;

    /* Rounded up, so that a wait of less than a millisecond is one rather than
     * none: a zero timeout in a poll is a spin, and this is what is handed to
     * one */
    return (long)((soonest - now + 999999LL) / 1000000LL);
}

/* Whatever the machine wrote to the MIDI port, and whatever arrived on it */
static void carry_midi(void)
{
    uint8_t byte;

    while (acia_take_transmitted(ACIA_MIDI, &byte))
        midi_give(byte);

    midi_pump();

    while (acia_can_receive(ACIA_MIDI) && midi_take(&byte))
    {
        acia_receive(ACIA_MIDI, byte);
        acia_settled();

        /*
         * One at a time, because the byte has to be read out of the chip
         * before the next may go in and what reads it is the machine. Handing
         * over a second one now would be an overrun on every byte but the
         * last.
         */
        break;
    }
}

void interrupt_service(void)
{
    long long now;
    int i;

    if (!built)
        return;

    carry_midi();

    now = now_ns();

    for (i = 0; i < SOURCES; i++)
    {
        struct source *s = &sources[i];
        int fired = 0;

        if (s->period_ns == 0)
            continue;

        while (s->due_ns <= now && fired < CATCHUP_MOST)
        {
            s->due_ns += s->period_ns;
            fired++;

            if (s->channel >= 0)
                mfp_raise(s->channel);

            /*
             * The counters the machine keeps. They are what a program reads
             * when it wants to know how long something took, and they are kept
             * whether or not anybody installed a handler - on an ST they were
             * TOS's, not the program's.
             */
            if (i == 2)
                poke_system_long(SYSVAR_HZ200, (uint32_t)++hz200_count);

            if (i == VBL_SOURCE)
            {
                ++vbl_count;
                poke_system_long(SYSVAR_VBCLOCK, (uint32_t)vbl_count);
                poke_system_long(SYSVAR_FRCLOCK, (uint32_t)vbl_count);
            }
        }

        /* Too far behind to catch up, which is the machine having been stopped
         * rather than the timer having been slow - the host was suspended, or
         * the emulator was sitting in a debugger. Start again from now. */
        if (s->due_ns <= now)
        {
            s->due_ns = now + s->period_ns;
            said("a timer fell so far behind that what it missed was dropped, "
                 "which is the host having been stopped rather than the "
                 "emulated machine having been slow");
        }
    }
}

void interrupt_tick(void)
{
    if (!built)
        return;

    if (--countdown > 0)
        return;

    countdown = TICK_INSTRUCTIONS;

    interrupt_service();
}
