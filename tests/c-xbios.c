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

/* One check per XBIOS function.
 *
 * The expected values are the contract each function promises, so this doubles
 * as documentation of what tosemu answers and catches a stub that quietly
 * changes its mind.
 *
 * Several XBIOS calls report nothing, and MiNTLib declares those void. For
 * them the contract is only that they return at all rather than halting the
 * emulator, which is what survived() records: if the call had halted, no
 * further output would appear.
 */

#include <stdio.h>
#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/trap14.h>

/* Waveplay is only bound in mint/sysbind.h, which cannot be included next to
 * osbind.h without redefining every trap macro, so bind it here */
#ifndef Waveplay
# define Waveplay(a,b,c,d) \
    (long)trap_14_wwlll(0xa5,(short)(a),(long)(b),(long)(c),(long)(d))
#endif

#define E_DRVNR     (-2)

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

static void survived(const char *name)
{
    n++;
    printf("ok %d - %s returns\n", n, name);
}

static char buffer[1024];
static long dspx, dspy;

int main(int argc, char **argv)
{
    long previous;

    /* 0x04 Getrez - deliberately not a real ST resolution, so that code
     * depending on the screen hardware fails loudly rather than misdrawing */
    check(Getrez(), 8, "Getrez reports a resolution no ST has");

    /* Floppy and DMA. There is no controller, so every operation on a disk
     * reports that the drive is not ready. Nothing pretends to have written. */
    check(Floprd(buffer, 0L, 0, 1, 0, 0, 1), E_DRVNR, "Floprd reports EDRVNR");
    check(Flopwr(buffer, 0L, 0, 1, 0, 0, 1), E_DRVNR, "Flopwr reports EDRVNR");
    check(Flopfmt(buffer, 0L, 0, 9, 0, 0, 1, 0x87654321L, 0xe5),
          E_DRVNR, "Flopfmt reports EDRVNR");
    check(Flopver(buffer, 0L, 0, 1, 0, 0, 1), E_DRVNR, "Flopver reports EDRVNR");
    check(DMAread(0L, 1, buffer, 0), E_DRVNR, "DMAread reports EDRVNR");
    check(DMAwrite(0L, 1, buffer, 0), E_DRVNR, "DMAwrite reports EDRVNR");
    Protobt(buffer, 0x12345678L, 2, 1);
    survived("Protobt");

    /* 0x29 Floprate - a setting rather than an operation on a disk, so it
     * round trips and reports the rate it replaced */
    previous = Floprate(0, -1);
    check(Floprate(0, 2), previous, "Floprate returns the previous rate");
    check(Floprate(0, -1), 2, "Floprate reports what was set");
    Floprate(0, 3);

    /* Sound, and the GI chip that drives it. No audio hardware. */
    check(Giaccess(0, 0), 0, "Giaccess reports 0");
    Offgibit(0xff);
    survived("Offgibit");
    Ongibit(0);
    survived("Ongibit");
    Dosound(buffer);
    survived("Dosound");
    check(Locksnd(), 1, "Locksnd grants the lock");
    check(Unlocksnd(), 0, "Unlocksnd releases it");
    check(Soundcmd(0, 0), 0, "Soundcmd reports 0");
    check(Setbuffer(0, buffer, buffer + sizeof buffer), 0, "Setbuffer reports 0");
    check(Setmode(0), 0, "Setmode reports 0");
    check(Settracks(0, 0), 0, "Settracks reports 0");
    check(Setmontracks(0), 0, "Setmontracks reports 0");
    check(Setinterrupt(0, 0), 0, "Setinterrupt reports 0");
    check(Buffoper(0), 0, "Buffoper reports 0");
    check(Dsptristate(0, 0), 0, "Dsptristate reports 0");
    check(Gpio(0, 0), 0, "Gpio reports 0");
    check(Devconnect(0, 0, 0, 0, 0), 0, "Devconnect reports 0");
    check(Sndstatus(0), 0, "Sndstatus reports 0");
    check(Buffptr(buffer), 0, "Buffptr reports 0");
    check(Waveplay(0, 0L, 0L, 0L), -1, "Waveplay reports an error");

    /* The DSP is absent, consistently, so that an application takes its
     * no-DSP path instead of a half working one */
    dspx = 0x1234;
    dspy = 0x5678;
    Dsp_Available(&dspx, &dspy);
    check(dspx, 0, "Dsp_Available reports no X memory");
    check(dspy, 0, "Dsp_Available reports no Y memory");
    check(Dsp_Lock(), -1, "Dsp_Lock refuses");
    check(Dsp_Reserve(0L, 0L), -1, "Dsp_Reserve refuses");
    check(Dsp_GetWordSize(), 0, "Dsp_GetWordSize reports 0");
    check(Dsp_GetProgAbility(), 0, "Dsp_GetProgAbility reports 0");
    check(Dsp_HStat(), 0, "Dsp_HStat reports 0");
    check(Dsp_Hf0(-1), 0, "Dsp_Hf0 reports 0");
    check(Dsp_Hf1(-1), 0, "Dsp_Hf1 reports 0");
    check(Dsp_Hf2(), 0, "Dsp_Hf2 reports 0");
    check(Dsp_Hf3(), 0, "Dsp_Hf3 reports 0");
    check(Dsp_InqSubrAbility(0), 0, "Dsp_InqSubrAbility reports 0");
    check(Dsp_LoadProg(buffer, 0, buffer), 0, "Dsp_LoadProg reports 0");
    check(Dsp_LoadSubroutine(buffer, 0L, 0), 0, "Dsp_LoadSubroutine reports 0");
    check(Dsp_LodToBinary(buffer, buffer), 0, "Dsp_LodToBinary reports 0");
    check(Dsp_RequestUniqueAbility(), 0, "Dsp_RequestUniqueAbility reports 0");
    check(Dsp_RunSubroutine(0), 0, "Dsp_RunSubroutine reports 0");

    Dsp_BlkBytes(buffer, 0L, buffer, 0L);
    survived("Dsp_BlkBytes");
    Dsp_BlkHandShake(buffer, 0L, buffer, 0L);
    survived("Dsp_BlkHandShake");
    Dsp_BlkUnpacked(buffer, 0L, buffer, 0L);
    survived("Dsp_BlkUnpacked");
    Dsp_BlkWords(buffer, 0L, buffer, 0L);
    survived("Dsp_BlkWords");
    Dsp_DoBlock(buffer, 0L, buffer, 0L);
    survived("Dsp_DoBlock");
    Dsp_ExecBoot(buffer, 0L, 0);
    survived("Dsp_ExecBoot");
    Dsp_ExecProg(buffer, 0L, 0);
    survived("Dsp_ExecProg");
    Dsp_FlushSubroutines();
    survived("Dsp_FlushSubroutines");
    Dsp_InStream(buffer, 0L, 0L, buffer);
    survived("Dsp_InStream");
    Dsp_IOStream(buffer, buffer, 0L, 0L, 0L, buffer);
    survived("Dsp_IOStream");
    Dsp_MultBlocks(0L, 0L, buffer, buffer);
    survived("Dsp_MultBlocks");
    Dsp_OutStream(buffer, 0L, 0L, buffer);
    survived("Dsp_OutStream");
    Dsp_RemoveInterrupts(0);
    survived("Dsp_RemoveInterrupts");
    Dsp_SetVectors(0L, 0L);
    survived("Dsp_SetVectors");
    Dsp_TriggerHC(0);
    survived("Dsp_TriggerHC");
    Dsp_Unlock();
    survived("Dsp_Unlock");

    printf("# %d checks, %d failed\n", n, fails);

    return fails;
}
