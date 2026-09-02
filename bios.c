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

#include "bios.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "tossystem.h"
#include "cpu.h"
#include "m68k.h"
#include "utils.h"
#include "drives.h"

#define BIOS_TRACE_CONTEXT
#include "config.h"

/* BIOS return values, http://toshyp.atari.org/en/003003.html */

#define BIOS_E_OK    (0)
#define BIOS_ERROR   (-1) /* Generic error */
#define BIOS_EDRVNR  (-2) /* Drive not ready */

uint32_t BIOS_Setexc()
{
    uint16_t nm = peek_u16(2);
    uint32_t vec = peek_u32(4);
    uint32_t old;

    FUNC_TRACE_ENTER_ARGS {
        printf("    nm: 0x%x, vec: 0x%x\n", nm, vec);
    }

    old = m68k_read_memory_32(4*nm);

    /* -1 asks for the current vector without installing a new one,
     * http://toshyp.atari.org/en/003004.html */
    if (vec != 0xffffffff)
        m68k_write_memory_32(4*nm, vec);

    return old;
}

/* Character devices *********************************************************/

/* The devices an ST addresses through Bconin and friends,
 * http://toshyp.atari.org/en/003003.html
 *
 * Only the console goes anywhere on the host. The others are accepted and
 * discarded rather than refused: an application writing to the printer should
 * carry on doing whatever it does next, not stall on a port that will never
 * become ready.
 */
#define DEV_PRT     (0) /* Parallel printer */
#define DEV_AUX     (1) /* Serial port */
#define DEV_CON     (2) /* Console, i.e. the screen and keyboard */
#define DEV_MIDI    (3) /* MIDI port */
#define DEV_IKBD    (4) /* Keyboard controller */
#define DEV_RAWCON  (5) /* Console without the line-editing the VT52 does */

static int is_console(uint16_t dev)
{
    return dev == DEV_CON || dev == DEV_RAWCON;
}

uint32_t BIOS_Bconin()
{
    uint16_t dev = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    dev: 0x%x\n", dev);
    }

    if (is_console(dev) && console_input_available())
        return getchar() & 0xff;

    /* Nothing arrives from a device that is not there */
    return 0;
}

uint32_t BIOS_Bconout()
{
    uint16_t dev = peek_u16(2);
    uint16_t c = peek_u16(4);

    FUNC_TRACE_ENTER_ARGS {
        printf("    dev: 0x%x, c: 0x%x '%c'\n", dev, c, c);
    }

    if (is_console(dev))
        putchar(c);

    /* Bytes for the printer, the serial port, MIDI and the keyboard
     * controller have nowhere to go */

    return 0;
}

uint32_t BIOS_Bconstat()
{
    uint16_t dev = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    dev: 0x%x\n", dev);
    }

    if (is_console(dev) && console_input_available())
        return -1;

    return 0;
}

uint32_t BIOS_Bcostat()
{
    uint16_t dev = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    dev: 0x%x\n", dev);
    }

    /* Every device reports ready, including the ones that discard what they
     * are given. An application waiting for a port to drain would otherwise
     * spin for ever on a device that is never going to answer. */
    return -1;
}

/* Timer functions ***********************************************************/

uint32_t BIOS_Tickcal()
{
    FUNC_TRACE_ENTER

    /* The system timer runs at 50 Hz on every ST, so a tick is 20 ms */
    return 20;
}

/* Drive functions ***********************************************************/

uint32_t BIOS_Drvmap()
{
    FUNC_TRACE_ENTER

    return drive_map();
}

uint32_t BIOS_Mediach()
{
    uint16_t dev = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    dev: 0x%x\n", dev);
    }

    return drive_mediach(dev);
}

/* Keyboard functions ********************************************************/

/* The host tells us nothing about the state of the modifier keys, so remember
 * what the application last set and report that back to it. An application
 * that sets a state and reads it again then sees its own value, rather than a
 * zero it never asked for. */
static uint32_t kbshift_state;

uint32_t BIOS_Kbshift()
{
    int16_t mode = peek_s16(2);
    uint32_t previous = kbshift_state;

    FUNC_TRACE_ENTER_ARGS {
        printf("    mode: %d\n", mode);
    }

    if (mode >= 0)
        kbshift_state = mode;

    return previous;
}

/* Memory functions **********************************************************/

uint32_t BIOS_Getmpb()
{
    uint32_t ptr = peek_u32(2);
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    ptr: 0x%x\n", ptr);
    }

    /* An _MPB is three pointers to the OS memory descriptor lists. tosemu
     * manages memory itself, see gemdosmem.c, so there are no lists to point
     * at. Zero the structure rather than leaving the caller with whatever its
     * buffer happened to hold. */
    for (i = 0; i < 3; ++i)
        m68k_write_memory_32(ptr + 4*i, 0);

    return BIOS_E_OK;
}

/* Table of non-implemented BIOS functions */

#define BIOS_Getbpb NULL
#define BIOS_Rwabs NULL

/* What a table entry does when it has no implementation.
 *
 * A machine tosemu does not emulate still has to answer, or the application
 * dies on a call it only made to ask a question. A stubbed function returns
 * the documented value that tells the application the operation did not
 * happen, which is not the same as pretending it succeeded.
 */
#define FN_HALT (0) /* Nothing decided yet, halt and say so */
#define FN_STUB (1) /* No host equivalent, answer with ret */

/* BIOS function table according to
 * http://www.yardley.cc/atari/compendium/atari-compendium-BIOS-Function-Reference.htm
 */
struct BIOS_function {
    char *name;
    uint32_t (*fnct)();
    uint16_t id;
    uint8_t kind;
    int32_t ret;
};

struct BIOS_function BIOS_functions[] = {
    {"Bconin", BIOS_Bconin, 0x02, FN_HALT, 0},
    {"Bconout", BIOS_Bconout, 0x03, FN_HALT, 0},
    {"Bconstat", BIOS_Bconstat, 0x01, FN_HALT, 0},
    {"Bcostat", BIOS_Bcostat, 0x08, FN_HALT, 0},
    {"Drvmap", BIOS_Drvmap, 0x0A, FN_HALT, 0},
    /* No BIOS parameter block to hand out, there is no TOS file system here */
    {"Getbpb", BIOS_Getbpb, 0x07, FN_STUB, 0},
    {"Getmpb", BIOS_Getmpb, 0x00, FN_HALT, 0},
    {"Kbshift", BIOS_Kbshift, 0x0B, FN_HALT, 0},
    {"Mediach", BIOS_Mediach, 0x09, FN_HALT, 0},
    /* Drives are backed by host directories, so there are no sectors to move.
     * Answering "drive not ready" is honest; a fake success would let an
     * application believe it had written something. */
    {"Rwabs", BIOS_Rwabs, 0x04, FN_STUB, BIOS_EDRVNR},
    {"Setexc", BIOS_Setexc, 0x05, FN_HALT, 0},
    {"Tickcal", BIOS_Tickcal, 0x06, FN_HALT, 0}
};

void bios_trap()
{
    uint16_t fnct = peek_u16(0);
    int i;

    for(i=0; i<sizeof(BIOS_functions)/sizeof(struct BIOS_function); ++i) {
        struct BIOS_function *f = &BIOS_functions[i];
        uint32_t r;

        if (f->id != fnct)
            continue;

        if (f->fnct) {
            r = f->fnct();
        } else if (f->kind == FN_STUB) {
            r = f->ret;
#ifdef ENABLE_BIOS_TRACE
            printf("Stubbed %s (0x%x)\n", f->name, fnct);
#endif
        } else {
            halt_execution();
            printf("BIOS %s (0x%x) not implemented\n", f->name, fnct);
            return;
        }

#ifdef ENABLE_BIOS_TRACE
        printf("Return from %s: %d = 0x%x\n", f->name, r, r);
#endif
        m68k_set_reg(M68K_REG_D0, r);

        return;
    }

    halt_execution();
    printf("BIOS Unknown function called 0x%x\n", fnct);
}
