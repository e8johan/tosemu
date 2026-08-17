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
 * Only the console has anywhere to go on the host, so the rest divide into
 * two. Settings, such as the serial configuration or the keyboard repeat rate,
 * are remembered and reported back, so that an application which configures a
 * device and reads the configuration sees its own value. Traffic, such as
 * bytes written to MIDI, is discarded.
 *
 * Nothing here interrupts. tosemu runs the application on a single thread with
 * no timer and no device raising anything, so Mfpint, Jenabint, Jdisint and
 * Xbtimer record nothing and do nothing. This is where an interrupt subsystem
 * would attach if one is ever built; Setexc in bios.c already maintains the
 * vector table it would dispatch through.
 */

#include "xbios.h"

#include <stdio.h>
#include <string.h>

#include "tossystem.h"
#include "cpu.h"
#include "m68k.h"

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

    FUNC_TRACE_ENTER_ARGS {
        printf("    cnt: %d, ptr: 0x%x\n", cnt, ptr);
    }

    /* Nothing is listening on the MIDI port, so the bytes go nowhere */
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

/* An IOREC describes the ring buffer a device fills from its interrupt
 * handler. Nothing fills them here, so hand out buffers that stay empty:
 * head equal to tail is how a reader knows there is nothing waiting. */
#define IOREC_DEVICES (4)
#define IOREC_SIZE    (14)
#define IOREC_BUFSIZE (256)

static uint32_t iorec[IOREC_DEVICES];

uint32_t XBIOS_Iorec()
{
    uint16_t dev = peek_u16(2);
    uint32_t buffer;

    FUNC_TRACE_ENTER_ARGS {
        printf("    dev: %d\n", dev);
    }

    if (dev >= IOREC_DEVICES)
        return 0;

    if (!iorec[dev])
    {
        iorec[dev] = bios_static_alloc(IOREC_SIZE);
        buffer = bios_static_alloc(IOREC_BUFSIZE);

        m68k_write_memory_32(iorec[dev] + 0, buffer);      /* ibuf     */
        m68k_write_memory_16(iorec[dev] + 4, IOREC_BUFSIZE); /* ibufsiz */
        m68k_write_memory_16(iorec[dev] + 6, 0);           /* ibufhd   */
        m68k_write_memory_16(iorec[dev] + 8, 0);           /* ibuftl   */
        m68k_write_memory_16(iorec[dev] + 10, IOREC_BUFSIZE/4); /* ibuflow  */
        m68k_write_memory_16(iorec[dev] + 12, IOREC_BUFSIZE*3/4); /* ibufhi */
    }

    return iorec[dev];
}

/* Keyboard vectors **********************************************************/

/* A _KBDVECS is nine vectors and a state byte. TOS points them at its own
 * handlers so that an application can chain onto one. Nothing calls them here,
 * but an application that reads a vector, saves it and installs its own must
 * find something it can restore later rather than reading uninitialised
 * memory. */
#define KBDVECS_SIZE (9*4 + 2)

static uint32_t kbdvecs;

uint32_t XBIOS_Kbdvbase()
{
    FUNC_TRACE_ENTER

    if (!kbdvecs)
        kbdvecs = bios_static_alloc(KBDVECS_SIZE);

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

/* Nothing in tosemu raises an interrupt, so all four of these accept what they
 * are given and do nothing with it. They are together, and named, so that it
 * is clear where an interrupt subsystem would attach. */

uint32_t XBIOS_Mfpint()
{
    uint16_t interno = peek_u16(2);
    uint32_t vector = peek_u32(4);

    FUNC_TRACE_ENTER_ARGS {
        printf("    interno: %d, vector: 0x%x\n", interno, vector);
    }

    return XBIOS_E_OK;
}

uint32_t XBIOS_Jenabint()
{
    uint16_t interno = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    interno: %d\n", interno);
    }

    return XBIOS_E_OK;
}

uint32_t XBIOS_Jdisint()
{
    uint16_t interno = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    interno: %d\n", interno);
    }

    return XBIOS_E_OK;
}

uint32_t XBIOS_Xbtimer()
{
    uint16_t timer = peek_u16(2);
    uint16_t control = peek_u16(4);
    uint16_t data = peek_u16(6);
    uint32_t vector = peek_u32(8);

    FUNC_TRACE_ENTER_ARGS {
        printf("    timer: %d, control: 0x%x, data: 0x%x, vector: 0x%x\n",
               timer, control, data, vector);
    }

    return XBIOS_E_OK;
}
