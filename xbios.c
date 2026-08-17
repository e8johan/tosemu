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

#include "xbios.h"

#include <stdio.h>
#include <stdlib.h>

#include "tossystem.h"
#include "memory.h"
#include "cpu.h"
#include "m68k.h"

#include "xbios_p.h"

/* XBIOS functions */

/* Floppy functions **********************************************************/

/* There is no floppy controller, but the seek rate is a setting rather than an
 * operation on a disk, so remember it and report the value it replaced. The
 * two drives share one rate on an ST. */
static uint32_t floprate = 3; /* 3 ms, the TOS default */

uint32_t XBIOS_Floprate()
{
    uint16_t dev = peek_u16(2);
    int16_t rate = peek_s16(4);
    uint32_t previous = floprate;

    FUNC_TRACE_ENTER_ARGS {
        printf("    dev: 0x%x, rate: %d\n", dev, rate);
    }

    if (rate >= 0)
        floprate = rate;

    return previous;
}

/* DSP functions *************************************************************/

/* There is no DSP. Every Dsp_ call answers so that an application takes its
 * no-DSP path rather than a half working one: Dsp_Lock and Dsp_Reserve refuse,
 * and nothing reports any capacity. */

uint32_t XBIOS_Dsp_Available()
{
    uint32_t xavail = peek_u32(2);
    uint32_t yavail = peek_u32(6);

    FUNC_TRACE_ENTER_ARGS {
        printf("    xavail: 0x%x, yavail: 0x%x\n", xavail, yavail);
    }

    /* Reports through pointers rather than in D0, so it needs to write the
     * zeroes rather than leave the caller reading its own buffer */
    m68k_write_memory_32(xavail, 0);
    m68k_write_memory_32(yavail, 0);

    return XBIOS_E_OK;
}

/* Keyboard table functions **************************************************/

uint32_t XBIOS_Keytbl()
{
    uint32_t unshift = peek_u32(2);
    uint32_t shift = peek_u32(6);
    uint32_t capslock = peek_u32(10);

    FUNC_TRACE_ENTER_ARGS {
        printf("    unshift : 0x%x\n    shift   : 0x%x\n    capslock: 0x%x\n", unshift, shift, capslock);
    }

    /* TODO to support writing to these tables, the keyboard mapping needs to
     * be supported in general. At the moment, the system relies on the mapping
     * of the host system.
     */
    if (unshift != 0xffffffff)
    {
        printf("XBIOS Keytbl: Altering the keyboard table is not supported (unshift)\n"); /* TODO */
        halt_execution();
    }
    if (shift != 0xffffffff)
    {
        printf("XBIOS Keytbl: Altering the keyboard table is not supported (shift)\n"); /* TODO */
        halt_execution();
    }
    if (capslock != 0xffffffff)
    {
        printf("XBIOS Keytbl: Altering the keyboard table is not supported (capslock)\n"); /* TODO */
        halt_execution();
    }

    return 0; /* TODO return a pointer to the table in some pre-allocated place in ST RAM */
}
uint32_t XBIOS_Bioskeys()
{
    /* TODO this is a nop, as we do not use the keyboard tables at the moment */
    return 0;
}


/* Table of non-implemented XBIOS functions */

#define XBIOS_Blitmode NULL
#define XBIOS_Buffoper NULL
#define XBIOS_Buffptr NULL
#define XBIOS_Devconnect NULL
#define XBIOS_DMAread NULL
#define XBIOS_DMAwrite NULL
#define XBIOS_Dosound NULL
#define XBIOS_Dsp_BlkBytes NULL
#define XBIOS_Dsp_BlkHandShake NULL
#define XBIOS_Dsp_BlkUnpacked NULL
#define XBIOS_Dsp_BlkWords NULL
#define XBIOS_Dsp_DoBlock NULL
#define XBIOS_Dsp_ExecBoot NULL
#define XBIOS_Dsp_ExecProg NULL
#define XBIOS_Dsp_FlushSubroutines NULL
#define XBIOS_Dsp_GetProgAbility NULL
#define XBIOS_Dsp_GetWordSize NULL
#define XBIOS_Dsp_Hf0 NULL
#define XBIOS_Dsp_Hf1 NULL
#define XBIOS_Dsp_Hf2 NULL
#define XBIOS_Dsp_Hf3 NULL
#define XBIOS_Dsp_HStat NULL
#define XBIOS_Dsp_InqSubrAbility NULL
#define XBIOS_Dsp_InStream NULL
#define XBIOS_Dsp_IOStream NULL
#define XBIOS_Dsp_LoadProg NULL
#define XBIOS_Dsp_LoadSubroutine NULL
#define XBIOS_Dsp_Lock NULL
#define XBIOS_Dsp_LodToBinary NULL
#define XBIOS_Dsp_MultBlocks NULL
#define XBIOS_Dsp_OutStream NULL
#define XBIOS_Dsp_RemoveInterrupts NULL
#define XBIOS_Dsp_RequestUniqueAbility NULL
#define XBIOS_Dsp_Reserve NULL
#define XBIOS_Dsp_RunSubroutine NULL
#define XBIOS_Dsp_SetVectors NULL
#define XBIOS_Dsp_TriggerHC NULL
#define XBIOS_Dsp_Unlock NULL
#define XBIOS_Dsptristate NULL
#define XBIOS_EgetShift NULL
#define XBIOS_EsetBank NULL
#define XBIOS_EsetGray NULL
#define XBIOS_EsetShift NULL
#define XBIOS_EsetSmear NULL
#define XBIOS_Flopfmt NULL
#define XBIOS_Floprd NULL
#define XBIOS_Flopver NULL
#define XBIOS_Flopwr NULL
#define XBIOS_Giaccess NULL
#define XBIOS_Gpio NULL
#define XBIOS_Locksnd NULL
#define XBIOS_Offgibit NULL
#define XBIOS_Ongibit NULL
#define XBIOS_Protobt NULL
#define XBIOS_Puntaes NULL
#define XBIOS_Scrdmp NULL
#define XBIOS_Setbuffer NULL
#define XBIOS_Setinterrupt NULL
#define XBIOS_Setmode NULL
#define XBIOS_Setmontracks NULL
#define XBIOS_Settracks NULL
#define XBIOS_Sndstatus NULL
#define XBIOS_Soundcmd NULL
#define XBIOS_Ssbrk NULL
#define XBIOS_Unlocksnd NULL
#define XBIOS_VgetMonitor NULL
#define XBIOS_VsetMask NULL
#define XBIOS_VsetSync NULL
#define XBIOS_Vsync NULL
#define XBIOS_Waveplay NULL

/* What a table entry does when it has no implementation, see bios.c */
#define FN_HALT (0) /* Nothing decided yet, halt and say so */
#define FN_STUB (1) /* No host equivalent, answer with ret */

/* XBIOS function table according to
 * http://www.yardley.cc/atari/compendium/atari-compendium-XBIOS-Function-Reference.htm
 */
struct XBIOS_function {
    char *name;
    uint32_t (*fnct)();
    uint16_t id;
    uint8_t kind;
    int32_t ret;
};

struct XBIOS_function XBIOS_functions[] = {
    {"Bconmap", XBIOS_Bconmap, 0x2C, FN_HALT, 0},
    {"Bioskeys", XBIOS_Bioskeys, 0x18, FN_HALT, 0},
    {"Blitmode", XBIOS_Blitmode, 0x40, FN_STUB, 0},
    {"Buffoper", XBIOS_Buffoper, 0x88, FN_STUB, 0},
    {"Buffptr", XBIOS_Buffptr, 0x8D, FN_STUB, 0},
    {"Cursconf", XBIOS_Cursconf, 0x15, FN_HALT, 0},
    {"Dbmsg", XBIOS_Dbmsg, 0x0B, FN_HALT, 0},
    {"Devconnect", XBIOS_Devconnect, 0x8B, FN_STUB, 0},
    {"DMAread", XBIOS_DMAread, 0x2A, FN_STUB, XBIOS_EDRVNR},
    {"DMAwrite", XBIOS_DMAwrite, 0x2B, FN_STUB, XBIOS_EDRVNR},
    {"Dosound", XBIOS_Dosound, 0x20, FN_STUB, 0},
    {"Dsp_Available", XBIOS_Dsp_Available, 0x6A, FN_HALT, 0},
    {"Dsp_BlkBytes", XBIOS_Dsp_BlkBytes, 0x7C, FN_STUB, 0},
    {"Dsp_BlkHandShake", XBIOS_Dsp_BlkHandShake, 0x61, FN_STUB, 0},
    {"Dsp_BlkUnpacked", XBIOS_Dsp_BlkUnpacked, 0x62, FN_STUB, 0},
    {"Dsp_BlkWords", XBIOS_Dsp_BlkWords, 0x7B, FN_STUB, 0},
    {"Dsp_DoBlock", XBIOS_Dsp_DoBlock, 0x60, FN_STUB, 0},
    {"Dsp_ExecBoot", XBIOS_Dsp_ExecBoot, 0x6E, FN_STUB, 0},
    {"Dsp_ExecProg", XBIOS_Dsp_ExecProg, 0x6D, FN_STUB, 0},
    {"Dsp_FlushSubroutines", XBIOS_Dsp_FlushSubroutines, 0x73, FN_STUB, 0},
    {"Dsp_GetProgAbility", XBIOS_Dsp_GetProgAbility, 0x72, FN_STUB, 0},
    {"Dsp_GetWordSize", XBIOS_Dsp_GetWordSize, 0x67, FN_STUB, 0},
    {"Dsp_Hf0", XBIOS_Dsp_Hf0, 0x77, FN_STUB, 0},
    {"Dsp_Hf1", XBIOS_Dsp_Hf1, 0x78, FN_STUB, 0},
    {"Dsp_Hf2", XBIOS_Dsp_Hf2, 0x79, FN_STUB, 0},
    {"Dsp_Hf3", XBIOS_Dsp_Hf3, 0x7A, FN_STUB, 0},
    {"Dsp_HStat", XBIOS_Dsp_HStat, 0x7D, FN_STUB, 0},
    {"Dsp_InqSubrAbility", XBIOS_Dsp_InqSubrAbility, 0x75, FN_STUB, 0},
    {"Dsp_InStream", XBIOS_Dsp_InStream, 0x63, FN_STUB, 0},
    {"Dsp_IOStream", XBIOS_Dsp_IOStream, 0x65, FN_STUB, 0},
    {"Dsp_LoadProg", XBIOS_Dsp_LoadProg, 0x6C, FN_STUB, 0},
    {"Dsp_LoadSubroutine", XBIOS_Dsp_LoadSubroutine, 0x74, FN_STUB, 0},
    {"Dsp_Lock", XBIOS_Dsp_Lock, 0x68, FN_STUB, -1},
    {"Dsp_LodToBinary", XBIOS_Dsp_LodToBinary, 0x6F, FN_STUB, 0},
    {"Dsp_MultBlocks", XBIOS_Dsp_MultBlocks, 0x7F, FN_STUB, 0},
    {"Dsp_OutStream", XBIOS_Dsp_OutStream, 0x64, FN_STUB, 0},
    {"Dsp_RemoveInterrupts", XBIOS_Dsp_RemoveInterrupts, 0x66, FN_STUB, 0},
    {"Dsp_RequestUniqueAbility", XBIOS_Dsp_RequestUniqueAbility, 0x71, FN_STUB, 0},
    {"Dsp_Reserve", XBIOS_Dsp_Reserve, 0x6B, FN_STUB, -1},
    {"Dsp_RunSubroutine", XBIOS_Dsp_RunSubroutine, 0x76, FN_STUB, 0},
    {"Dsp_SetVectors", XBIOS_Dsp_SetVectors, 0x7E, FN_STUB, 0},
    {"Dsp_TriggerHC", XBIOS_Dsp_TriggerHC, 0x70, FN_STUB, 0},
    {"Dsp_Unlock", XBIOS_Dsp_Unlock, 0x69, FN_STUB, 0},
    {"Dsptristate", XBIOS_Dsptristate, 0x89, FN_STUB, 0},
    {"EgetPalette", XBIOS_EgetPalette, 0x55, FN_HALT, 0},
    {"EgetShift", XBIOS_EgetShift, 0x51, FN_STUB, 0},
    {"EsetBank", XBIOS_EsetBank, 0x52, FN_STUB, 0},
    {"EsetColor", XBIOS_EsetColor, 0x53, FN_HALT, 0},
    {"EsetGray", XBIOS_EsetGray, 0x56, FN_STUB, 0},
    {"EsetPalette", XBIOS_EsetPalette, 0x54, FN_HALT, 0},
    {"EsetShift", XBIOS_EsetShift, 0x50, FN_STUB, 0},
    {"EsetSmear", XBIOS_EsetSmear, 0x57, FN_STUB, 0},
    {"Flopfmt", XBIOS_Flopfmt, 0x0A, FN_STUB, XBIOS_EDRVNR},
    {"Floprate", XBIOS_Floprate, 0x29, FN_HALT, 0},
    {"Floprd", XBIOS_Floprd, 0x08, FN_STUB, XBIOS_EDRVNR},
    {"Flopver", XBIOS_Flopver, 0x13, FN_STUB, XBIOS_EDRVNR},
    {"Flopwr", XBIOS_Flopwr, 0x09, FN_STUB, XBIOS_EDRVNR},
    {"Getrez",      XBIOS_Getrez, 0x04, FN_HALT, 0},
    {"Gettime", XBIOS_Gettime, 0x17, FN_HALT, 0},
    {"Giaccess", XBIOS_Giaccess, 0x1C, FN_STUB, 0},
    {"Gpio", XBIOS_Gpio, 0x8A, FN_STUB, 0},
    {"Ikbdws", XBIOS_Ikbdws, 0x19, FN_HALT, 0},
    {"Initmous", XBIOS_Initmous, 0x00, FN_HALT, 0},
    {"Iorec", XBIOS_Iorec, 0x0E, FN_HALT, 0},
    {"Jdisint", XBIOS_Jdisint, 0x1A, FN_HALT, 0},
    {"Jenabint", XBIOS_Jenabint, 0x1B, FN_HALT, 0},
    {"Kbdvbase", XBIOS_Kbdvbase, 0x22, FN_HALT, 0},
    {"Kbrate", XBIOS_Kbrate, 0x23, FN_HALT, 0},
    {"Keytbl", XBIOS_Keytbl, 0x10, FN_HALT, 0},
    {"Locksnd", XBIOS_Locksnd, 0x80, FN_STUB, 1},
    {"Logbase", XBIOS_Logbase, 0x03, FN_HALT, 0},
    {"Metainit", XBIOS_Metainit, 0x30, FN_HALT, 0},
    {"Mfpint", XBIOS_Mfpint, 0x0D, FN_HALT, 0},
    {"Midiws", XBIOS_Midiws, 0x0C, FN_HALT, 0},
    {"NVMaccess", XBIOS_NVMaccess, 0x2E, FN_HALT, 0},
    {"Offgibit", XBIOS_Offgibit, 0x1D, FN_STUB, 0},
    {"Ongibit", XBIOS_Ongibit, 0x1E, FN_STUB, 0},
    {"Physbase", XBIOS_Physbase, 0x02, FN_HALT, 0},
    {"Protobt", XBIOS_Protobt, 0x12, FN_STUB, 0},
    {"Prtblk", XBIOS_Prtblk, 0x24, FN_HALT, 0},
    {"Puntaes", XBIOS_Puntaes, 0x27, FN_STUB, 0},
    {"Random", XBIOS_Random, 0x11, FN_HALT, 0},
    {"Rsconf", XBIOS_Rsconf, 0x0F, FN_HALT, 0},
    {"Scrdmp", XBIOS_Scrdmp, 0x14, FN_STUB, 0},
    {"Setbuffer", XBIOS_Setbuffer, 0x83, FN_STUB, 0},
    {"Setcolor", XBIOS_Setcolor, 0x07, FN_HALT, 0},
    {"Setinterrupt", XBIOS_Setinterrupt, 0x87, FN_STUB, 0},
    {"Setmode", XBIOS_Setmode, 0x84, FN_STUB, 0},
    {"Setmontracks", XBIOS_Setmontracks, 0x86, FN_STUB, 0},
    {"Setpalette", XBIOS_Setpalette, 0x06, FN_HALT, 0},
    {"Setprt", XBIOS_Setprt, 0x21, FN_HALT, 0},
    /* VsetScreen shares this id. It is the Falcon superset of the same
     * call and the two cannot be told apart from the stack, so Setscreen
     * answers for both. */
    {"Setscreen", XBIOS_Setscreen, 0x05, FN_HALT, 0},
    {"Settime", XBIOS_Settime, 0x16, FN_HALT, 0},
    {"Settracks", XBIOS_Settracks, 0x85, FN_STUB, 0},
    {"Sndstatus", XBIOS_Sndstatus, 0x8C, FN_STUB, 0},
    {"Soundcmd", XBIOS_Soundcmd, 0x82, FN_STUB, 0},
    {"Ssbrk", XBIOS_Ssbrk, 0x01, FN_STUB, 0},
    {"Supexec", XBIOS_Supexec, 0x26, FN_HALT, 0},
    {"Unlocksnd", XBIOS_Unlocksnd, 0x81, FN_STUB, 0},
    {"VgetMonitor", XBIOS_VgetMonitor, 0x59, FN_STUB, 0},
    {"VgetRGB", XBIOS_VgetRGB, 0x5E, FN_HALT, 0},
    {"VgetSize", XBIOS_VgetSize, 0x5B, FN_HALT, 0},
    {"VsetMask", XBIOS_VsetMask, 0x96, FN_STUB, 0},
    {"VsetMode", XBIOS_VsetMode, 0x58, FN_HALT, 0},
    {"VsetRGB", XBIOS_VsetRGB, 0x5D, FN_HALT, 0},
    {"VsetSync", XBIOS_VsetSync, 0x5A, FN_STUB, 0},
    {"Vsync", XBIOS_Vsync, 0x25, FN_STUB, 0},
    {"Waveplay", XBIOS_Waveplay, 0xA5, FN_STUB, XBIOS_ERROR},
    {"Xbtimer", XBIOS_Xbtimer, 0x1F, FN_HALT, 0}
};

void xbios_trap()
{
    uint16_t fnct = peek_u16(0);
    int i;

    for(i=0; i<sizeof(XBIOS_functions)/sizeof(struct XBIOS_function); ++i) {
        struct XBIOS_function *f = &XBIOS_functions[i];
        uint32_t r;

        if (f->id != fnct)
            continue;

        if (f->fnct) {
            r = f->fnct();
        } else if (f->kind == FN_STUB) {
            r = f->ret;
#ifdef ENABLE_XBIOS_TRACE
            printf("Stubbed %s (0x%x)\n", f->name, fnct);
#endif
        } else {
            halt_execution();
            printf("XBIOS %s (0x%x) not implemented\n", f->name, fnct);
            return;
        }

#ifdef ENABLE_XBIOS_TRACE
        printf("Return from %s: %d = 0x%x\n", f->name, r, r);
#endif
        m68k_set_reg(M68K_REG_D0, r);

        return;
    }

    halt_execution();
    printf("XBIOS Unknown function called 0x%x\n", fnct);
}
