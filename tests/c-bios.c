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

/* One check per BIOS function.
 *
 * The expected values are the contract each function promises, so this doubles
 * as documentation of what tosemu answers and catches a stub that quietly
 * changes its mind. Bconin and Bconstat are not checked, they would block or
 * depend on what is waiting on stdin.
 */

#include <stdio.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>

#define DRIVE_C     (2)
#define MAP_C       (1L << DRIVE_C)
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

static char sector[512];
static _MPB mpb;

int main(int argc, char **argv)
{
    long previous;

    /* 0x00 Getmpb - tosemu manages memory itself, so the lists are empty */
    mpb.mp_free = (_MD *)0x1234;
    mpb.mp_used = (_MD *)0x5678;
    mpb.mp_rover = (_MD *)0x9abc;
    Getmpb(&mpb);
    check((long)mpb.mp_free, 0, "Getmpb clears mp_free");
    check((long)mpb.mp_used, 0, "Getmpb clears mp_used");
    check((long)mpb.mp_rover, 0, "Getmpb clears mp_rover");

    /* 0x03 Bconout, and 0x08 Bcostat which must report ready or an
     * application waiting to send spins forever */
    check(Bcostat(2), -1, "Bcostat console is ready");

    /* 0x04 Rwabs - no sectors behind a host directory */
    check(Rwabs(0, sector, 1, 0, DRIVE_C), E_DRVNR, "Rwabs reports EDRVNR");

    /* 0x06 Tickcal - the system timer is 50 Hz on every ST */
    check(Tickcal(), 20, "Tickcal reports 20 ms per tick");

    /* 0x07 Getbpb - no TOS file system, so no parameter block */
    check((long)Getbpb(DRIVE_C), 0, "Getbpb reports no BPB");

    /* 0x09 Mediach - a host directory never has its media swapped */
    check(Mediach(DRIVE_C), 0, "Mediach reports no change on C:");

    /* 0x0A Drvmap - must agree with what GEMDOS says the drives are */
    check(Drvmap(), MAP_C, "Drvmap reports C: only");
    check(Drvmap(), Dsetdrv(Dgetdrv()), "Drvmap agrees with Dsetdrv");

    /* 0x0B Kbshift - the host reports no key state, so what is set is what
     * comes back, and a set returns the state it replaced */
    previous = Kbshift(-1);
    check(Kbshift(0x03), previous, "Kbshift returns the previous state");
    check(Kbshift(-1), 0x03, "Kbshift reports what was set");
    Kbshift(0);
    check(Kbshift(-1), 0, "Kbshift can be cleared");

    /* 0x05 Setexc - reading a vector must not disturb it. Vector 5 is the
     * divide by zero exception, which nothing in the test uses. */
    previous = (long)Setexc(5, -1L);
    check((long)Setexc(5, -1L), previous, "Setexc -1 only reads the vector");

    printf("# %d checks, %d failed\n", n, fails);

    return fails;
}
