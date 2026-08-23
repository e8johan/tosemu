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
#include <mint/ostruct.h>
#include <mint/falcon.h>
#include <mint/trap14.h>

/* Waveplay is only bound in mint/sysbind.h, which cannot be included next to
 * osbind.h without redefining every trap macro, so bind it here */
#ifndef Waveplay
# define Waveplay(a,b,c,d) \
    (long)trap_14_wwlll(0xa5,(short)(a),(long)(b),(long)(c),(long)(d))
#endif

/* Metainit is bound in mint/metados.h and Dbmsg only in mint/sysbind.h,
 * neither of which can be included next to osbind.h, so bind them here */
#ifndef Metainit
# define Metainit(buffer) (void)trap_14_wl((short)0x30,(long)(buffer))
#endif
#ifndef Dbmsg
# define Dbmsg(a,b,c) \
    (void)trap_14_wwwl((short)0x0b,(short)(a),(short)(b),(long)(c))
#endif

/* mint/falcon.h binds VsetMask through trap_14_www, which takes two arguments
 * where VsetMask passes three, so it does not compile. Bind it correctly. */
#undef VsetMask
#define VsetMask(ormask,andmask,overlay) \
    (short)trap_14_wllw(150,(long)(ormask),(long)(andmask),(short)(overlay))

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
static short palette[256];
static _IOREC *iorec;
static long rgb[16];

int main(int argc, char **argv)
{
    long previous;
    int i;

    /* 0x04 Getrez - deliberately not a real ST resolution, so that code
     * depending on the screen hardware fails loudly rather than misdrawing */
    check(Getrez(), 8, "Getrez reports a resolution no ST has");

    /* Screen and video. Nothing reaches a display, but an application that
     * draws needs somewhere to draw and an application that configures the
     * video hardware needs its settings to hold. */
    check(Physbase() != 0L, 1, "Physbase hands out a buffer");
    check((long)Logbase(), (long)Physbase(), "Logbase starts on the same buffer");

    /* The buffer is the screen this machine has, and this runs on the default
     * one - 640x400 in a single plane, which comes to 32000 bytes. That the
     * size follows the screen rather than being a number is c-screen's to
     * check, it being the test that runs on every screen there is. */
    check(VgetSize(0), 32000, "VgetSize is the size of the default screen");

    /* The buffer must really be writable, an application will paint into it */
    *(char *)Physbase() = 0x5a;
    check(*(char *)Physbase(), 0x5a, "the screen buffer holds what is written");

    Setscreen(-1L, -1L, -1);
    check((long)Physbase(), (long)Logbase(), "Setscreen -1 changes nothing");

    /* Palettes round trip, so code that sets a colour and reads it back sees
     * its own value */
    previous = Setcolor(1, 0x777);
    check(Setcolor(1, -1), 0x777, "Setcolor reports what was set");
    check(Setcolor(1, previous), 0x777, "Setcolor returns the previous colour");

    for (i = 0; i < 16; i++)
        palette[i] = 0x111 * (i & 7);
    Setpalette(palette);
    survived("Setpalette");
    check(Setcolor(3, -1), 0x333, "Setpalette set the whole palette");

    palette[0] = 0x123;
    EsetPalette(32, 1, palette);
    palette[0] = 0;
    EgetPalette(32, 1, palette);
    check(palette[0], 0x123, "EsetPalette and EgetPalette round trip");
    check(EsetColor(32, -1), 0x123, "EsetColor sees the same palette");

    rgb[0] = 0x00112233L;
    VsetRGB(0, 1, rgb);
    rgb[0] = 0;
    VgetRGB(0, 1, rgb);
    check(rgb[0], 0x00112233L, "VsetRGB and VgetRGB round trip");

    previous = VsetMode(VERTFLAG);
    check(VsetMode(-1), VERTFLAG, "VsetMode reports the mode that was set");
    VsetMode(previous);

    check(Cursconf(5, 0), 10, "Cursconf reports the default blink rate");
    Cursconf(4, 25);
    check(Cursconf(5, 0), 25, "Cursconf remembers a new blink rate");
    Cursconf(4, 10);

    check(Blitmode(-1), 0, "Blitmode reports no blitter");
    check(EgetShift(), 0, "EgetShift reports 0");
    check(EsetBank(-1), 0, "EsetBank reports 0");
    check(EsetGray(-1), 0, "EsetGray reports 0");
    check(EsetSmear(-1), 0, "EsetSmear reports 0");
    check(VgetMonitor(), 0, "VgetMonitor reports 0");
    check(VsetMask(0L, 0L, 0), 0, "VsetMask reports 0");
    EsetShift(0);
    survived("EsetShift");
    VsetSync(0);
    survived("VsetSync");
    Vsync();
    survived("Vsync");
    Scrdmp();
    survived("Scrdmp");

    /* System and clock. These have a real equivalent on the host. */
    check(Gettime() != 0L, 1, "Gettime reports a time");
    check((Gettime() >> 25) >= 40, 1, "Gettime reports a year past 2020");
    Settime(0L);
    survived("Settime");
    check(Gettime() != 0L, 1, "Settime left the host clock alone");
    check((Random() & 0xff000000L), 0, "Random stays within 24 bits");
    check(Random() != Random(), 1, "Random gives different values");
    check((long)Ssbrk(16), 0, "Ssbrk reports 0");

    /* Battery backed memory, which round trips within a run */
    buffer[0] = 0x5a;
    buffer[1] = 0xa5;
    check(NVMaccess(1, 4, 2, buffer), 0, "NVMaccess writes");
    buffer[0] = buffer[1] = 0;
    check(NVMaccess(0, 4, 2, buffer), 0, "NVMaccess reads");
    check(buffer[0] & 0xff, 0x5a, "NVMaccess kept the first byte");
    check(buffer[1] & 0xff, 0xa5, "NVMaccess kept the second byte");
    check(NVMaccess(2, 0, 0, buffer), 0, "NVMaccess clears");
    NVMaccess(0, 4, 2, buffer);
    check(buffer[0] & 0xff, 0, "NVMaccess really cleared it");
    check(NVMaccess(0, 60, 2, buffer) != 0, 1, "NVMaccess rejects a bad range");

    /* No MetaDOS, reported by zeroing the caller's structure */
    for (i = 0; i < 12; i++)
        buffer[i] = 0x7f;
    Metainit(buffer);
    check(buffer[0] | buffer[4] | buffer[8], 0, "Metainit reports no MetaDOS");

    Dbmsg(0x5abc, 0, 0L);
    survived("Dbmsg");

    /* Devices. Settings round trip, traffic is discarded, and nothing
     * interrupts. */
    previous = Rsconf(-1, -1, -1, -1, -1, -1);
    check(Rsconf(-1, -1, -1, -1, -1, -1), previous, "Rsconf -1 changes nothing");
    Rsconf(-1, -1, 0x12, 0x34, 0x56, 0x78);
    check(Rsconf(-1, -1, -1, -1, -1, -1), 0x12345678L,
          "Rsconf reports the registers that were set");

    previous = Bconmap(-1);
    check(previous, 6, "Bconmap reports the one serial port");
    check(Bconmap(7), 6, "Bconmap returns the previous mapping");
    Bconmap(6);

    previous = Setprt(-1);
    check(Setprt(0x2b), previous, "Setprt returns the previous config");
    check(Setprt(-1), 0x2b, "Setprt reports what was set");
    Setprt(previous);

    previous = Kbrate(-1, -1);
    check(Kbrate(-1, -1), previous, "Kbrate -1 changes nothing");
    Kbrate(30, 4);
    check(Kbrate(-1, -1), (30 << 8) | 4, "Kbrate reports what was set");

    /* An empty IOREC: a reader sees head equal to tail and stops */
    iorec = (_IOREC *)Iorec(0);
    check(iorec != 0L, 1, "Iorec hands out a record");
    check(iorec->ibufhd, iorec->ibuftl, "the input buffer reads as empty");
    check(iorec->ibufsiz != 0, 1, "the input buffer has a size");
    check((long)Iorec(0), (long)iorec, "Iorec keeps handing out the same one");

    /* Vectors an application can save and restore, even though none is
     * ever called */
    check((long)Kbdvbase() != 0L, 1, "Kbdvbase hands out the vectors");

    Midiws(0, buffer);
    survived("Midiws");
    Ikbdws(0, buffer);
    survived("Ikbdws");
    Initmous(0, 0L, 0L);
    survived("Initmous");
    Prtblk(buffer);
    survived("Prtblk");
    Mfpint(0, 0L);
    survived("Mfpint");
    Jenabint(0);
    survived("Jenabint");
    Jdisint(0);
    survived("Jdisint");
    Xbtimer(0, 0, 0, 0L);
    survived("Xbtimer");

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
