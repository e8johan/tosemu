/*
 * TOSEMU - an emulated environment for TOS applications
 * Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
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
 * The devices hanging off an ST: the serial port, the printer, MIDI, the
 * keyboard controller, and the MFP that interrupts on their behalf.
 *
 * The console and MIDI have somewhere to go on the host, so the rest divide
 * into two. Settings, such as the serial configuration or the keyboard repeat
 * rate, are remembered and reported back, so that an application which
 * configures a device and reads the configuration sees its own value. Traffic
 * with nowhere to go, such as bytes written to the printer, is discarded.
 *
 * Whether anything here interrupts depends on the machine. On the ordinary one
 * nothing does, and Mfpint, Jenabint, Jdisint and Xbtimer accept what they are
 * given and do nothing with it, as they always have. On a machine that was
 * asked for interrupts - by naming a MIDI port, or by saying so - they are how
 * a program reaches the MFP, and what it installs is called. See interrupt.h.
 */

#include "xbios.h"

#include <stdio.h>
#include <string.h>

#include "tossystem.h"
#include "cpu.h"
#include "m68k.h"
#include "midi.h"
#include "interrupt.h"
#include "mfp.h"
#include "iorec.h"
#include "memory.h"

#include "xbios_p.h"

/* Serial port ***************************************************************/

/* Rsconf reports the previous setting of whichever register it changed, and an
 * application asking without setting passes -1 */
static uint32_t rsconf_speed = 7;  /* 9600 baud, the TOS default */
static uint32_t rsconf_flow;
static uint32_t rsconf_ucr = 0x88;
static uint32_t rsconf_rsr;
static uint32_t rsconf_tsr;
static uint32_t rsconf_scr;

static void rsconf_set(uint32_t *reg, int16_t value)
{
    if (value >= 0)
        *reg = value;
}

uint32_t XBIOS_Rsconf()
{
    int16_t speed = peek_s16(2);
    int16_t flow = peek_s16(4);
    int16_t ucr = peek_s16(6);
    int16_t rsr = peek_s16(8);
    int16_t tsr = peek_s16(10);
    int16_t scr = peek_s16(12);
    uint32_t previous;

    FUNC_TRACE_ENTER_ARGS {
        printf("    speed: %d, flow: %d, ucr: 0x%x, rsr: 0x%x, tsr: 0x%x, scr: 0x%x\n",
               speed, flow, ucr, rsr, tsr, scr);
    }

    /* The old values of the four MFP registers, packed one per byte,
     * http://toshyp.atari.org/en/004008.html */
    previous = (rsconf_ucr << 24) | (rsconf_rsr << 16) |
               (rsconf_tsr << 8) | rsconf_scr;

    rsconf_set(&rsconf_speed, speed);
    rsconf_set(&rsconf_flow, flow);
    rsconf_set(&rsconf_ucr, ucr);
    rsconf_set(&rsconf_rsr, rsr);
    rsconf_set(&rsconf_tsr, tsr);
    rsconf_set(&rsconf_scr, scr);

    return previous;
}

uint32_t XBIOS_Bconmap()
{
    int16_t dev = peek_s16(2);
    static uint32_t mapped = 6; /* The ST maps one serial port, device 6 */
    uint32_t previous = mapped;

    FUNC_TRACE_ENTER_ARGS {
        printf("    dev: %d\n", dev);
    }

    /* -1 asks which device is mapped, -2 asks for the mapping table, which
     * there is no point in building for a machine with one serial port */
    if (dev >= 0)
        mapped = dev;

    return previous;
}

/* Printer *******************************************************************/

uint32_t XBIOS_Setprt()
{
    int16_t config = peek_s16(2);
    static uint32_t printer_config; /* Dot matrix, mono, draft, parallel */
    uint32_t previous = printer_config;

    FUNC_TRACE_ENTER_ARGS {
        printf("    config: %d\n", config);
    }

    if (config >= 0)
        printer_config = config;

    return previous;
}

uint32_t XBIOS_Prtblk()
{
    uint32_t pblkptr = peek_u32(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    pblkptr: 0x%x\n", pblkptr);
    }

    /* Printing a screen dump needs both a screen and a printer */
    return XBIOS_E_OK;
}

/* MIDI and the keyboard controller ******************************************/

uint32_t XBIOS_Midiws()
{
    uint16_t cnt = peek_u16(2);
    uint32_t ptr = peek_u32(4);
    uint32_t i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    cnt: %d, ptr: 0x%x\n", cnt, ptr);
    }

    /*
     * One more byte than the count, the argument being how many to send less
     * one. It is read unsigned, which is what TOS did and what makes a count
     * of -1 sixty five thousand bytes rather than none - a program that means
     * to send nothing does not call this at all.
     */
    for (i = 0; i <= cnt; i++)
        midi_give((uint8_t)m68k_read_memory_8(ptr + i));

    /* Once, rather than after each byte: this is the call a program sends a
     * whole message with, and the message is what the far end is waiting for */
    midi_pump();

    return XBIOS_E_OK;
}

uint32_t XBIOS_Ikbdws()
{
    uint16_t cnt = peek_u16(2);
    uint32_t ptr = peek_u32(4);

    FUNC_TRACE_ENTER_ARGS {
        printf("    cnt: %d, ptr: 0x%x\n", cnt, ptr);
    }

    /* There is no keyboard controller to command. An application setting the
     * mouse mode or the clock this way gets no answer either way. */
    return XBIOS_E_OK;
}

uint32_t XBIOS_Initmous()
{
    uint16_t type = peek_u16(2);
    uint32_t param = peek_u32(4);
    uint32_t vec = peek_u32(8);

    FUNC_TRACE_ENTER_ARGS {
        printf("    type: %d, param: 0x%x, vec: 0x%x\n", type, param, vec);
    }

    /* The mouse packet handler would be called from an interrupt, and nothing
     * here interrupts, so the vector is accepted and never used */
    return XBIOS_E_OK;
}

/* Input buffers *************************************************************/

/*
 * An IOREC describes the ring buffer a device fills from its interrupt handler
 * and whatever reads the device empties.
 *
 * The record is in the machine's memory rather than in ours, because that is
 * the point of it: Iorec hands the application its address, and period MIDI
 * software reads the buffer directly rather than going through Bconin - which
 * was the quick way to take a stream of notes in on an eight megahertz
 * machine. So the fields below are read and written where they lie, and the
 * arithmetic on them is iorec.c's, which is TOS's.
 *
 * The serial port and the printer have no interrupt handler here and so stay
 * empty for ever, which is what a reader finds out from head being equal to
 * tail.
 */
#define IOREC_DEVICES (4)
#define IOREC_SIZE    (14)
#define IOREC_BUFSIZE (256)

#define IOREC_MIDI (2)

/* Where each field sits in the record */
#define IOREC_IBUF    (0)
#define IOREC_IBUFSIZ (4)
#define IOREC_IBUFHD  (6)
#define IOREC_IBUFTL  (8)
#define IOREC_IBUFLOW (10)
#define IOREC_IBUFHI  (12)

static uint32_t iorec[IOREC_DEVICES];

/* The record for a device, made the first time anybody wants one - which is
 * either the application asking for its address or a byte arriving on it */
static uint32_t iorec_for(int dev)
{
    uint32_t buffer;

    if (dev < 0 || dev >= IOREC_DEVICES)
        return 0;

    if (!iorec[dev])
    {
        iorec[dev] = bios_static_alloc(IOREC_SIZE);
        buffer = bios_static_alloc(IOREC_BUFSIZE);

        if (!iorec[dev] || !buffer)
        {
            iorec[dev] = 0;
            return 0;
        }

        m68k_write_memory_32(iorec[dev] + IOREC_IBUF, buffer);
        m68k_write_memory_16(iorec[dev] + IOREC_IBUFSIZ, IOREC_BUFSIZE);
        m68k_write_memory_16(iorec[dev] + IOREC_IBUFHD, 0);
        m68k_write_memory_16(iorec[dev] + IOREC_IBUFTL, 0);
        m68k_write_memory_16(iorec[dev] + IOREC_IBUFLOW, IOREC_BUFSIZE/4);
        m68k_write_memory_16(iorec[dev] + IOREC_IBUFHI, IOREC_BUFSIZE*3/4);
    }

    return iorec[dev];
}

/*
 * The record as something iorec.c can work on.
 *
 * The buffer is read through the machine's own address for it rather than
 * through the one this handed out, because an application is allowed to point
 * the record at a buffer of its own and some do - a program expecting a great
 * deal of MIDI gives it somewhere larger to go. EmuTOS's own handler says as
 * much where it loads the pointer: "must use ptr, user may have changed it".
 */
static int iorec_view(int dev, struct iorec_ring *r)
{
    uint32_t rec = iorec_for(dev);

    if (!rec)
        return 0;

    r->buf = tos_mem_to_host_mem(m68k_read_disassembler_32(rec + IOREC_IBUF));
    r->size = (int)m68k_read_disassembler_16(rec + IOREC_IBUFSIZ);
    r->head = (int)m68k_read_disassembler_16(rec + IOREC_IBUFHD);
    r->tail = (int)m68k_read_disassembler_16(rec + IOREC_IBUFTL);

    return r->buf != 0 && r->size > 0;
}

/* A byte from a device's interrupt handler. Answers 0 when the ring was full
 * and it was dropped, which is the only thing a device can do about it. */
int xbios_iorec_push(int dev, uint8_t byte)
{
    struct iorec_ring r;

    if (!iorec_view(dev, &r))
        return 0;

    if (!iorec_push(&r, byte))
        return 0;

    m68k_write_memory_16(iorec[dev] + IOREC_IBUFTL, (uint16_t)r.tail);

    return 1;
}

/* And a byte out of it, which is what Bconin does */
int xbios_iorec_take(int dev, uint8_t *byte)
{
    struct iorec_ring r;

    if (!iorec_view(dev, &r))
        return 0;

    if (!iorec_take(&r, byte))
        return 0;

    m68k_write_memory_16(iorec[dev] + IOREC_IBUFHD, (uint16_t)r.head);

    return 1;
}

/* How many are waiting, which is what Bconstat answers */
int xbios_iorec_count(int dev)
{
    struct iorec_ring r;

    if (!iorec_view(dev, &r))
        return 0;

    return iorec_count(&r);
}

uint32_t XBIOS_Iorec()
{
    uint16_t dev = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    dev: %d\n", dev);
    }

    if (dev >= IOREC_DEVICES)
        return 0;

    return iorec_for(dev);
}

/* Keyboard vectors **********************************************************/

/*
 * A _KBDVECS is nine vectors and a state byte, and midivec - the first of them
 * - is where a byte arriving on the MIDI port is taken.
 *
 * TOS pointed it at a routine of its own that put the byte in the IOREC, and
 * the documented way for an application to watch MIDI is to read that address,
 * keep it, put its own there, and jump to the one it kept when it has finished.
 * Which means the address it reads has to be something it can actually jump to.
 * Nought would do for an application that simply replaces the vector and never
 * looks at what was there, and would send one that chains to address nought.
 *
 * So midivec points at two bytes of magic memory that read as an RTS and put
 * the byte in the IOREC on the way past - the same trick Supexec uses to get
 * control back, see magic_xbios_supexec_read in xbiossys.c. What an
 * application chains to is then a real routine that does what TOS's did.
 *
 * The other eight point at an RTS and nothing else, which is what TOS pointed
 * the ones it had no use for at.
 */
#define KBDVECS_SIZE (9*4 + 2)

#define KBDVECS_MIDIVEC (0)

static uint32_t kbdvecs;

/* The two bytes that read as an RTS, and the two that read as an RTS and fill
 * the MIDI buffer on the way */
static uint32_t just_rts;
static uint32_t midivec_magic;

uint32_t xbios_midivec_magic(void)
{
    return midivec_magic;
}

/*
 * Where a byte arriving on the MIDI port should be taken, which is whatever is
 * in midivec - the application's routine if it installed one, ours if it did
 * not, and nought if nobody has ever asked for the vectors at all.
 */
uint32_t xbios_midivec(void)
{
    if (!kbdvecs)
        return 0;

    return m68k_read_disassembler_32(kbdvecs + KBDVECS_MIDIVEC);
}

static uint8_t magic_kbdvecs_read(struct _memarea *area, uint32_t address)
{
    /* 0x4e75 is RTS. The first byte says so and the second does the work,
     * which is the last moment before the instruction runs. */
    if (address == midivec_magic + 1)
        xbios_iorec_push(IOREC_MIDI, (uint8_t)m68k_get_reg(0, M68K_REG_D0));

    return (address & 1) ? 0x75 : 0x4e;
}

static void magic_kbdvecs_write(struct _memarea *area, uint32_t address,
                                uint8_t value)
{
    printf("Attempted to write to magic memory at 0x%x\n", address);
    halt_execution();
}

uint32_t XBIOS_Kbdvbase()
{
    int i;

    FUNC_TRACE_ENTER

    if (kbdvecs)
        return kbdvecs;

    kbdvecs = bios_static_alloc(KBDVECS_SIZE);
    midivec_magic = bios_device_alloc(2);
    just_rts = bios_device_alloc(2);

    if (!kbdvecs || !midivec_magic || !just_rts)
    {
        kbdvecs = 0;
        return 0;
    }

    /*
     * Areas of their own, over nothing. They are taken from the end of the
     * BIOS RAM that the plain area does not cover - see bios_device_alloc -
     * because two areas over one address answer according to the order they
     * went up in, and find_memarea remembers the last area it found rather
     * than walking the list again.
     */
    add_fnct_memory_area("midivec", MEMORY_READ | MEMORY_SUPERREAD,
                         midivec_magic, 2, 0,
                         magic_kbdvecs_read, magic_kbdvecs_write);
    add_fnct_memory_area("justrts", MEMORY_READ | MEMORY_SUPERREAD,
                         just_rts, 2, 0,
                         magic_kbdvecs_read, magic_kbdvecs_write);

    for (i = 0; i < 9; i++)
        m68k_write_memory_32(kbdvecs + 4*i, just_rts);

    m68k_write_memory_32(kbdvecs + KBDVECS_MIDIVEC, midivec_magic);

    return kbdvecs;
}

uint32_t XBIOS_Kbrate()
{
    int16_t delay = peek_s16(2);
    int16_t rate = peek_s16(4);
    static uint32_t kb_delay = 25; /* The TOS defaults */
    static uint32_t kb_rate = 5;
    uint32_t previous = (kb_delay << 8) | kb_rate;

    FUNC_TRACE_ENTER_ARGS {
        printf("    delay: %d, rate: %d\n", delay, rate);
    }

    if (delay >= 0)
        kb_delay = delay & 0xff;
    if (rate >= 0)
        kb_rate = rate & 0xff;

    return previous;
}

/* Interrupts ****************************************************************/

/*
 * The four calls a program sets the machine's own interrupts up with.
 *
 * On a machine that does not interrupt - which is the ordinary one, see
 * interrupt.h - all four accept what they are given and do nothing with it,
 * exactly as they always did. There is no chip to configure and nothing that
 * would ever call what was installed, and a program that asks anyway should
 * carry on rather than stop.
 *
 * On one that does, they are the whole of how a program reaches the MFP
 * without writing to it directly. Xbtimer is the one that matters: it is how a
 * sequencer starts its clock, and everything that follows from a sequencer
 * working follows from this line.
 */

/* Where the sixteen channels' vectors live, the MFP's vector base being 0x40 */
#define MFP_VECTOR_ADDRESS(channel) (0x100 + 4 * ((channel) & 15))

uint32_t XBIOS_Mfpint()
{
    uint16_t interno = peek_u16(2);
    uint32_t vector = peek_u32(4);

    FUNC_TRACE_ENTER_ARGS {
        printf("    interno: %d, vector: 0x%x\n", interno, vector);
    }

    if (!interrupt_wanted())
        return XBIOS_E_OK;

    /*
     * Turned off around the change, which is EmuTOS's mfpint and is not
     * tidiness: a channel that fired between the vector being written and the
     * channel being enabled would be taken through half an arrangement.
     */
    mfp_disable(interno & 15);
    m68k_write_memory_32(MFP_VECTOR_ADDRESS(interno), vector);
    mfp_enable(interno & 15);

    return XBIOS_E_OK;
}

uint32_t XBIOS_Jenabint()
{
    uint16_t interno = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    interno: %d\n", interno);
    }

    if (interrupt_wanted())
        mfp_enable(interno & 15);

    return XBIOS_E_OK;
}

uint32_t XBIOS_Jdisint()
{
    uint16_t interno = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    interno: %d\n", interno);
    }

    if (interrupt_wanted())
        mfp_disable(interno & 15);

    return XBIOS_E_OK;
}

uint32_t XBIOS_Xbtimer()
{
    uint16_t timer = peek_u16(2);
    uint16_t control = peek_u16(4);
    uint16_t data = peek_u16(6);
    uint32_t vector = peek_u32(8);
    int channel;

    FUNC_TRACE_ENTER_ARGS {
        printf("    timer: %d, control: 0x%x, data: 0x%x, vector: 0x%x\n",
               timer, control, data, vector);
    }

    if (!interrupt_wanted())
        return XBIOS_E_OK;

    channel = mfp_timer_channel(timer);

    if (channel < 0)
        return XBIOS_E_OK;

    mfp_setup_timer(timer, (uint8_t)control, (uint8_t)data);

    /* The same as Mfpint does, and in the same order, because that is what
     * Xbtimer is: setting a timer up and then pointing its channel somewhere */
    mfp_disable(channel);
    m68k_write_memory_32(MFP_VECTOR_ADDRESS(channel), vector);
    mfp_enable(channel);

    /* And the control register has changed, so how long it runs for has */
    interrupt_timers_changed();

    return XBIOS_E_OK;
}

void xbios_dev_reset()
{
    int i;

    for (i = 0; i < IOREC_DEVICES; i++)
        iorec[i] = 0;

    kbdvecs = 0;
    midivec_magic = 0;
    just_rts = 0;

    /*
     * And everything the machine was doing on behalf of the application that
     * has gone: the vectors it installed, the timers it started, and a message
     * half way down the MIDI cable. A system exclusive dump cut in half by a
     * Pexec would otherwise be finished off by the next program's first byte.
     */
    interrupt_reset();
    midi_reset();
}
