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
#include <poll.h>

#include "mfp.h"
#include "acia.h"
#include "midi.h"
#include "memory.h"
#include "settings.h"
#include "tossystem.h"
#include "cpu.h"
#include "xbios.h"
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
     * A program is setting the chip up on a machine where nothing will ever
     * go off. The registers are here and they keep what they are given, so it
     * will get back whatever it writes and then wait for an interrupt for
     * ever; saying so once is the difference between that and a hang nobody
     * can account for.
     *
     * Once, because a program configuring the MFP writes several registers and
     * the second one is no more news than the first.
     */
    if (!interrupt_wanted())
    {
        said("a program is programming the MFP, but this machine has no clock "
             "behind it and nothing will ever interrupt. Start it with "
             "interrupts - TOSEMU_INTERRUPTS in README.md - if it is waiting "
             "for one.");

        return;
    }

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

/* Below, with the rest of what the handler is - it needs the code it is
 * made of and the routine its magic byte reaches */
static void install_acia_handler(void);

void interrupt_init(void)
{
    int i;

    mfp_reset();
    acia_reset();

    /*
     * The chips are in the machine whether or not anything asked for them,
     * because they were in the machine. An ST has an MFP and two ACIAs at
     * these addresses always, and a program is entitled to read them without
     * having announced an interest in interrupts first - MROS reads both ACIAs
     * directly, and a program that only wants to know what the keyboard shift
     * state is reads the MFP. Mapping them only when interrupts were asked for
     * meant those programs stopped the emulator on an address that is part of
     * every one of these machines.
     *
     * It is worth doing now because it is free now. Every area used to be
     * another node on the list that each memory access walked, so a device
     * nobody was using still cost something on every instruction; one
     * remembered area took that away - see find_memarea.
     *
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

    /*
     * And the handler, which is a device in the same sense: bytes that are
     * read, with one of them doing something on the way past. Always, and for
     * the same reason the chips are always there - a program is entitled to
     * look at TOS's ACIA handler without having announced an interest in
     * interrupts, and what it finds has to be a handler.
     *
     * The vector goes in over the one tossystem.c filled the table with, which
     * is why this is after that and not before it.
     */
    install_acia_handler();

    /*
     * What is still asked for is the clock behind them. The chips answer
     * either way, but nothing counts down and nothing is ever raised unless
     * this machine was started with interrupts - which is what keeps a run
     * that wants none from reading the host's clock every few thousand
     * instructions. A program that programs a timer on a machine with no clock
     * is told once rather than left waiting - see mfp_area_write.
     */
    if (!interrupt_wanted())
        return;

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

    /*
     * And the MIDI ACIA, set up the way TOS set it: eight bits, no parity,
     * one stop bit, the clock divided by sixteen, and an interrupt when a byte
     * arrives. Its channel enabled for the same reason the timer's is - TOS
     * enabled it, and a program that never configures the port still expects
     * bytes to reach the buffer Iorec hands it.
     */
    acia_write(ACIA_MIDI, 0, 0x95);
    mfp_enable(MFP_ACIA);

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

    acia_write(ACIA_MIDI, 0, 0x95);
    mfp_enable(MFP_ACIA);

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

/* Taking an interrupt *******************************************************/

/*
 * Where the sixteen channels' vectors live. The MFP is set up with its vector
 * base at 0x40, so channel n goes through vector 0x40 + n, and a vector is
 * four bytes into the table at the bottom of memory.
 */
#define VECTOR_ADDRESS(channel) (0x100 + 4 * (channel))

/*
 * TOS's ACIA interrupt handler, as instructions in the machine's own memory.
 *
 * Everything else tosemu does for an interrupt it does in host C, and for the
 * ACIAs that was enough right up until a program wanted to read the handler
 * rather than be served by it. MROS - the MIDI kernel Cubase loads - takes the
 * vector at 0x118, walks the XBRA chain back from it, and then scans forward
 * through what it finds looking for the instruction that tests the keyboard
 * ACIA, so that it can call TOS's handler as a subroutine from that point.
 * There was nothing to find: the vector held a two byte return-from-exception,
 * and the scan ran off the end of whatever was mapped after it.
 *
 * So there is a handler here, and it is a real one. It reads as the code below
 * and runs as the code below, and the working parts are reached the way the
 * rest of tosemu's magic memory is reached - a byte whose being read is the
 * event. This is what that mechanism is for.
 *
 * Two things about the order the pieces are in, both of which are the whole
 * design rather than tidiness:
 *
 * The test against the keyboard ACIA is a separate routine rather than part of
 * the body, because what MROS does with the address it finds is jsr to it. It
 * has to be the start of something that returns.
 *
 * And the byte with the side effect on it is placed after that test rather
 * than before it. MROS's scan reads its way forward a word at a time, so
 * anything it passes over on the way is read - and a read is exactly what
 * makes the side effect happen. Putting the MIDI half first would mean that
 * merely looking for the handler took a byte off the port.
 */
#define ACIA_HANDLER_SIZE  (28)
#define ACIA_HANDLER_IKBD  (20)   /* the tst.b, which is what MROS looks for */
#define ACIA_HANDLER_MIDI  (26)   /* the byte whose being read does the work */

static const uint8_t acia_handler_code[ACIA_HANDLER_SIZE] = {
    0x48, 0xe7, 0xc0, 0xc0,             /*  0  movem.l d0-d1/a0-a1,-(sp)     */
    0x61, 0x14,                         /*  4  bsr.s   midi_side            */
    0x61, 0x0c,                         /*  6  bsr.s   ikbd_side            */
    0x11, 0xfc, 0x00, 0xbf, 0xfa, 0x11, /*  8  move.b  #0xbf,0xfffffa11     */
    0x4c, 0xdf, 0x03, 0x03,             /* 14  movem.l (sp)+,d0-d1/a0-a1    */
    0x4e, 0x73,                         /* 18  rte                          */
    0x4a, 0x38, 0xfc, 0x00,             /* 20  ikbd_side: tst.b 0xfffffc00  */
    0x4e, 0x75,                         /* 24  rts                          */
    0x4e, 0x75                          /* 26  midi_side: rts, and the byte */
};

/* Where it was put, and therefore what the vector holds */
static uint32_t acia_handler;

/*
 * Where a handler run from inside a trap is told to return to.
 *
 * Zero, and never fetched from: the loop below looks at the program counter
 * before it executes anything, so reaching this address is the signal to stop
 * rather than an instruction to run. The same trick and the same address as
 * the AES uses for a routine that draws an object - see USERDEF_RETURN.
 */
#define INTERRUPT_RETURN (0)

/* A handler that has not come back by now has lost its return address. A MIDI
 * handler is a few hundred instructions and a slow one a few thousand. */
#define INTERRUPT_STEPS (1000000L)

static int running;

int tos_int_ack(int level)
{
    int vector = M68K_INT_ACK_AUTOVECTOR;

    /*
     * Level six is the MFP's, and the MFP says which of its channels won and
     * therefore which vector. Everything else on this machine is autovectored:
     * the vertical blank comes in at level four straight from the video
     * hardware and has no chip deciding anything about it.
     */
    if (level == 6)
    {
        vector = mfp_acknowledge();

        if (vector < 0)
            vector = M68K_INT_ACK_SPURIOUS;
    }

    /*
     * And the line goes down, because nothing else will lower it. Musashi
     * clears the request for itself only when nobody acknowledges - which is
     * exactly the arrangement asking to acknowledge turns off - so a line left
     * up is the same interrupt taken again the instant the handler returns.
     *
     * Safe from in here: it sets the level to nothing and then looks for
     * something above the mask, and there is nothing above nothing. What is
     * still pending in the chip is raised again by the next tick.
     */
    m68k_set_irq(0);

    return vector;
}

/*
 * A channel nobody claimed.
 *
 * The vector is still nought, which on a real machine means jumping to
 * whatever is at address zero and on this one means Musashi reading the
 * uninitialised-interrupt vector, finding that nought as well, and running off
 * into memory that was never anything. So a channel with no handler is
 * answered here instead: the interrupt is taken, in the sense that it stops
 * being pending, and nothing is told about it.
 *
 * This is the ordinary case rather than an error. There is no TOS in this
 * machine, so until an application installs something every vector is empty -
 * and the system timer has been running since before the application started.
 */
/*
 * Run a routine of the machine's and come back, for the vectors that are
 * called rather than jumped to.
 *
 * The same shape as run_handler below, and simpler: what is on the end of one
 * of these is an ordinary subroutine ending in RTS, so there is no exception
 * frame to build - a return address on the stack is the whole of it.
 */
static void run_routine(uint32_t routine, uint32_t d0)
{
    uint32_t d[8], a[8], pc, sr, isp;
    long steps;
    int i;

    for (i = 0; i < 8; i++)
        d[i] = m68k_get_reg(0, M68K_REG_D0 + i);
    for (i = 0; i < 8; i++)
        a[i] = m68k_get_reg(0, M68K_REG_A0 + i);
    pc = m68k_get_reg(0, M68K_REG_PC);
    sr = m68k_get_reg(0, M68K_REG_SR);
    isp = m68k_get_reg(0, M68K_REG_ISP);

    /* In supervisor mode, because it is being called from an interrupt and
     * that is the mode one runs in */
    enable_supervisor_mode();

    push_u32(INTERRUPT_RETURN);

    m68k_set_reg(M68K_REG_D0, d0);
    m68k_set_reg(M68K_REG_PC, routine);

    running = 1;

    for (steps = 0; steps < INTERRUPT_STEPS; steps++)
    {
        if (m68k_get_reg(0, M68K_REG_PC) == INTERRUPT_RETURN)
            break;

        if (execution_halted())
            break;

        m68k_execute(1);
    }

    running = 0;

    if (steps >= INTERRUPT_STEPS)
    {
        halt_execution();
        printf("tosemu: the routine at 0x%x ran for %ld instructions without "
               "returning\n", routine, INTERRUPT_STEPS);
    }

    m68k_set_reg(M68K_REG_SR, sr);
    m68k_set_reg(M68K_REG_ISP, isp);
    for (i = 0; i < 8; i++)
        m68k_set_reg(M68K_REG_D0 + i, d[i]);
    for (i = 0; i < 8; i++)
        m68k_set_reg(M68K_REG_A0 + i, a[i]);
    m68k_set_reg(M68K_REG_PC, pc);
}

/*
 * A byte has arrived on the MIDI port and nobody has claimed the ACIA's
 * vector, so tosemu answers for it - which is to say, does what TOS's own
 * handler did.
 *
 * That handler read the byte out of the chip and handed it to midivec, and
 * midivec put it in the IOREC. Both halves matter: an application that watches
 * MIDI does it by replacing midivec, and one that does not expects to find the
 * bytes in the buffer Iorec told it about.
 *
 * Reading the byte is also what stops the chip asking. Leaving it unread would
 * leave the line into the MFP down and the channel pending for ever.
 */
static void acia_arrived(void)
{
    uint8_t byte = acia_read(ACIA_MIDI, 1);
    uint32_t vec;

    acia_settled();

    vec = xbios_midivec();

    /* Ours is two bytes of magic memory that read as an RTS and fill the
     * buffer on the way past. Running it would work and would mean executing
     * an instruction to do what can be done here. */
    if (vec == 0 || vec == xbios_midivec_magic())
    {
        xbios_iorec_push(2, byte);
        return;
    }

    run_routine(vec, byte);
}

/*
 * The handler as the machine reads it.
 *
 * Reading is the whole of what this device does. The instructions come back as
 * the bytes they are, and one of them - the second half of the return that
 * ends the MIDI side - does the work on its way past, which is the last moment
 * before that instruction runs. midivec's magic is the same trick at the same
 * place in the same word.
 */
static uint8_t acia_handler_read(struct _memarea *area, uint32_t address)
{
    uint32_t offset = address - acia_handler;

    (void)area;

    if (offset >= ACIA_HANDLER_SIZE)
        return 0xff;

    if (offset == ACIA_HANDLER_MIDI + 1)
        acia_arrived();

    return acia_handler_code[offset];
}

/*
 * And writing to it does nothing, quietly.
 *
 * This is TOS, and TOS was in ROM: a program that writes here would be writing
 * to a ROM on the machine, where the write goes nowhere and the program
 * carries on. Halting instead would turn something a real machine shrugs off
 * into a dead emulator. Said once, because a program patching what it takes
 * for a ROM is worth knowing about even though nothing came of it.
 */
static void acia_handler_write(struct _memarea *area, uint32_t address,
                               uint8_t value)
{
    (void)area;
    (void)address;
    (void)value;

    said("a program wrote to the ACIA interrupt handler, which on a real "
         "machine is in ROM. Nothing was changed.");
}

static void install_acia_handler(void)
{
    acia_handler = bios_device_alloc(ACIA_HANDLER_SIZE);

    if (!acia_handler)
        return;

    add_fnct_memory_area("aciahandler", MEMORY_READ | MEMORY_SUPERREAD,
                         acia_handler, ACIA_HANDLER_SIZE, 0,
                         acia_handler_read, acia_handler_write);

    poke_system_long(VECTOR_ADDRESS(MFP_ACIA), acia_handler);
}

static void handled_here(int channel)
{
    mfp_acknowledge();

    if (channel == MFP_ACIA)
        acia_arrived();

    /*
     * And finished, which matters as much as having done it.
     *
     * Acknowledging puts the channel in service, and a channel in service
     * holds off itself and everything below it until its handler says it is
     * done. Here tosemu is the handler, so tosemu has to say so - without this
     * the first byte of MIDI arrives and the second never does, the channel
     * having blocked itself for ever. An application's handler clears its own
     * bit and wants no help; this is only for the ones nobody claimed.
     */
    mfp_finished(channel);
}

/*
 * Run a handler to completion, for when there is no instruction stream to
 * interrupt.
 *
 * The natural way to take an interrupt is to leave it to Musashi, which builds
 * the frame and points the processor at the handler, and that is what happens
 * between instructions. It does not work from inside a trap: the emulator is
 * in host C with no m68k_execute running, so nothing would execute the handler
 * until the trap returned - and a wait that sleeps for a tenth of a second at
 * a time would hold every interrupt for that long. A sequencer waiting for GEM
 * between notes is exactly that case.
 *
 * So the frame is built by hand with a return address that is not real, and
 * the handler is run here until it comes back to it. The shape is
 * host_userdef_draw's in aestree.c, including the order things are put back
 * in, and the reasoning there applies here word for word.
 */
static void run_handler(int channel, uint32_t handler)
{
    uint32_t d[8], a[8], pc, sr, isp;
    long steps;
    int i;

    for (i = 0; i < 8; i++)
        d[i] = m68k_get_reg(0, M68K_REG_D0 + i);
    for (i = 0; i < 8; i++)
        a[i] = m68k_get_reg(0, M68K_REG_A0 + i);
    pc = m68k_get_reg(0, M68K_REG_PC);
    sr = m68k_get_reg(0, M68K_REG_SR);
    isp = m68k_get_reg(0, M68K_REG_ISP);

    /*
     * In supervisor mode, which is what makes a7 the supervisor stack - and
     * the supervisor stack is where an interrupt frame belongs. Unlike a
     * routine that draws an object, a handler is not the application's and
     * does not want the application's stack.
     */
    enable_supervisor_mode();

    /* An exception frame as a 68000 builds one: the program counter to come
     * back to, then the status register underneath it. RTE takes them off in
     * the other order. */
    push_u32(INTERRUPT_RETURN);
    push_u16((uint16_t)sr);

    /* Masked at the level being serviced, so that a handler is not interrupted
     * by its own channel going off again while it runs */
    m68k_set_reg(M68K_REG_SR, (sr & ~0x0700u) | 0x0600u | 0x2000u);
    m68k_set_reg(M68K_REG_PC, handler);

    running = 1;

    for (steps = 0; steps < INTERRUPT_STEPS; steps++)
    {
        if (m68k_get_reg(0, M68K_REG_PC) == INTERRUPT_RETURN)
            break;

        /* Anything that stops the machine stops this as well, or the rest of
         * the handler runs after the emulator has given up on it */
        if (execution_halted())
            break;

        m68k_execute(1);
    }

    running = 0;

    if (steps >= INTERRUPT_STEPS)
    {
        halt_execution();
        printf("tosemu: the handler at 0x%x on MFP channel %d ran for %ld "
               "instructions without returning\n",
               handler, channel, INTERRUPT_STEPS);
    }

    /*
     * The status register first, because it decides which of the two stack
     * pointers a7 is: putting the mode back afterwards would file the restored
     * a7 under the wrong one. Then the supervisor stack pointer by hand, the
     * machine's own not being where the handler was left standing.
     */
    m68k_set_reg(M68K_REG_SR, sr);
    m68k_set_reg(M68K_REG_ISP, isp);
    for (i = 0; i < 8; i++)
        m68k_set_reg(M68K_REG_D0 + i, d[i]);
    for (i = 0; i < 8; i++)
        m68k_set_reg(M68K_REG_A0 + i, a[i]);
    m68k_set_reg(M68K_REG_PC, pc);
}

/*
 * Whatever the chip has decided, turned into something the machine does about
 * it. `nested` says whether there is an instruction stream to interrupt: from
 * the instruction hook there is, and from inside a trap there is not.
 */
static void dispatch(int nested)
{
    int channel;
    uint32_t handler;

    /* A handler that reaches this has called something that services
     * interrupts - the AES, most likely - and running another one inside it
     * would be an interrupt interrupting itself */
    if (running)
        return;

    channel = mfp_pending_channel();

    if (channel < 0)
        return;

    handler = m68k_read_disassembler_32(VECTOR_ADDRESS(channel));

    /*
     * Nobody has claimed this channel. Nought would say so on a machine whose
     * vectors were never filled in, and this one's are: they hold an address
     * that returns from exception and does nothing else, because TOS filled
     * its table with ROM addresses and software reads them. So the question is
     * whether the vector is still that, and not whether it is nought.
     *
     * Asking the old question here is what made the MIDI buffer stop filling:
     * every vector became non-nought at once, so the ACIA's channel looked
     * claimed, the stub returned without taking the byte out of the chip, and
     * nothing ever arrived.
     */
    /*
     * And now there is a third thing a vector can hold and still be unclaimed:
     * tosemu's own ACIA handler. It is real code and it works, but running it
     * would mean the machine executing thirty-odd instructions to reach a byte
     * that host C already has in its hand. It is there to be read - scanned,
     * chained to, called into - by the programs that want to see a handler,
     * and those programs put their own address in the vector when they mean to
     * be called. Until one does, this is still nobody, and the byte goes the
     * short way.
     */
    if (handler == 0 || handler == tos_default_vector()
        || handler == acia_handler)
    {
        handled_here(channel);
        return;
    }

    if (!nested)
    {
        /*
         * Musashi does the rest: raising the line makes it acknowledge, build
         * the frame and jump, all before this returns. The handler runs when
         * the hook does, which is to say on the instruction that was about to
         * happen anyway.
         */
        m68k_set_irq(6);
        return;
    }

    if (mfp_acknowledge() < 0)
        return;

    run_handler(channel, handler);
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

/*
 * Look at the clock, do what has come due, and then let the machine deal with
 * it.
 *
 * `nested` is the one thing the two callers disagree about: whether there is
 * an instruction stream to interrupt. From the instruction hook there is, and
 * Musashi can be left to take the interrupt itself. From inside a trap there
 * is not - the emulator is in host C, and nothing would run the handler until
 * the trap returned.
 */
static void service(int nested)
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

    dispatch(nested);
}

void interrupt_service(void)
{
    /* From inside a trap, where the handler has to be run to completion before
     * whatever called this can carry on */
    service(1);
}

/*
 * Long enough that a program polling a silent port is not spinning, short
 * enough that it is not noticeable. Nothing depends on it for accuracy: it is
 * a floor under how often the caller looks again, and the timers have their
 * own answer to when they are due.
 */
#define WAIT_AT_MOST_MS (20)

void interrupt_wait(void)
{
    struct pollfd fds[1];
    int nfds = 0;
    long due;
    int wait;

    if (!built)
        return;

    due = interrupt_next_due_ms();

    wait = (due < 0 || due > WAIT_AT_MOST_MS) ? WAIT_AT_MOST_MS : (int)due;

    if (midi_fd() >= 0)
    {
        fds[0].fd = midi_fd();
        fds[0].events = POLLIN;
        nfds = 1;
    }

    /* A wait with nothing to wait on is still a wait: the timers are what
     * makes time pass here, and they are watched by the clock rather than by a
     * descriptor */
    if (nfds)
        poll(fds, nfds, wait);
    else
    {
        struct timespec t;

        t.tv_sec = 0;
        t.tv_nsec = (long)wait * 1000000L;
        nanosleep(&t, 0);
    }

    interrupt_service();
}

void interrupt_tick(void)
{
    if (!built)
        return;

    if (--countdown > 0)
        return;

    countdown = TICK_INSTRUCTIONS;

    /* Between two instructions, which is the only moment an interrupt may be
     * taken without the machine having half done something */
    service(0);
}
