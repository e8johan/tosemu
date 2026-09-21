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
 * The video shifter's registers, and the XBIOS calls that are the same
 * registers seen from the other side.
 *
 * A debugger is the program this is for. It keeps a screen of its own and
 * swaps between that and the program's by writing the video base and the
 * colours directly, and it finds out what the program's were by reading the
 * registers rather than by asking the XBIOS. So what matters is that the two
 * agree in both directions: what Setscreen and Setcolor set is what the
 * registers read, and what is written to the registers is what Physbase and
 * Setcolor report.
 *
 * The registers are read in supervisor mode, where an ST allows it. The
 * screen expected is named on the command line, since the resolution register
 * and the colours the machine starts in are both the screen's.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>

#define BASE_HI  ((volatile unsigned char *)0xffff8201L)
#define BASE_MID ((volatile unsigned char *)0xffff8203L)
#define BASE_LO  ((volatile unsigned char *)0xffff820dL)
#define SYNC     ((volatile unsigned char *)0xffff820aL)
#define REZ      ((volatile unsigned char *)0xffff8260L)
#define COLOURS  ((volatile unsigned short *)0xffff8240L)

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

static long base_registers(void)
{
    return ((long)*BASE_HI << 16) | ((long)*BASE_MID << 8) | *BASE_LO;
}

/* Somewhere else to point the shifter at, on the boundary an ST needed */
static char elsewhere[32000 + 256];

int main(int argc, char **argv)
{
    long ssp, phys, other;
    long read_base, read_after_setscreen, physbase_after_poke, low_after_hi;
    long rez, sync, colour0, ink, poked, setcolor_after_poke, masked;
    long v_bas_ad, logbase;
    int ink_index;
    short want_rez;

    if (argc < 2)
    {
        printf("Bail out! - no screen named to expect\n");
        return 1;
    }

    if (strcmp(argv[1], "low") == 0)
        want_rez = 0, ink_index = 15;
    else if (strcmp(argv[1], "medium") == 0)
        want_rez = 1, ink_index = 3;
    else
        want_rez = 2, ink_index = 1;

    phys = (long)Physbase();
    other = ((long)elsewhere + 255) & ~255L;

    ssp = Super(0L);

    read_base = base_registers();
    rez = *REZ;
    sync = *SYNC;
    colour0 = COLOURS[0];
    ink = COLOURS[ink_index];

    /* The XBIOS moving the screen moves the registers */
    Setscreen(-1L, (void *)other, -1);
    read_after_setscreen = base_registers();

    /* And moving the logical one moves _v_bas_ad, which is where TOS keeps it */
    logbase = (long)Logbase();
    Setscreen((void *)other, -1L, -1);
    v_bas_ad = *(volatile long *)0x44eL;
    Setscreen((void *)logbase, -1L, -1);

    /* And the registers moving it moves what the XBIOS says, which is the
     * way round a debugger goes. An STE clears the low byte when the high one
     * is written, so it is given one first to see it go. */
    *BASE_LO = 0x20;
    *BASE_HI = (unsigned char)(phys >> 16);
    low_after_hi = *BASE_LO;
    *BASE_MID = (unsigned char)(phys >> 8);
    physbase_after_poke = (long)Physbase();

    /* The same for a colour, both ways, and with more bits than a register
     * has room for */
    Setcolor(5, 0x0123);
    poked = COLOURS[5];
    COLOURS[5] = 0x0456;
    setcolor_after_poke = Setcolor(5, -1);
    COLOURS[5] = 0x7abc;
    masked = COLOURS[5];

    Super((void *)ssp);

    check(read_base, phys, "the video base registers are where Physbase is");
    check(rez, want_rez, "the resolution register is the screen's");
    check(rez, Getrez(), "and says what Getrez says");
    check(sync, 0x02, "the machine is running at fifty hertz");
    check(colour0, 0x0fff, "the background starts out white");
    check(ink, 0x0000, "and the ink starts out black");
    check(read_after_setscreen, other, "Setscreen moves the video base");
    check(v_bas_ad, other, "and the logical screen moves _v_bas_ad");
    check(low_after_hi, 0, "writing the high byte clears the low one");
    check(physbase_after_poke, phys, "and writing the registers moves Physbase");
    check(poked, 0x0123, "Setcolor sets the colour register");
    check(setcolor_after_poke, 0x0456,
          "and Setcolor reports what was written to it");
    check(masked, 0x0abc, "which holds four bits a gun and no more");

    printf("1..%d\n", n);

    return fails;
}
