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

/* The drive table. tosemu presents a single drive, C:, backed by the host
 * file system. These are the assertions a second drive has to keep working. */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>

#define DRIVE_C     (2)
#define MAP_C       (1L << DRIVE_C)
#define E_DRIVE     (-46)

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

int main(int argc, char **argv)
{
    char path[256];
    long h;

    check(Dgetdrv(), DRIVE_C, "Dgetdrv reports C:");
    check(Dsetdrv(DRIVE_C), MAP_C, "Dsetdrv C: returns the drive map");
    check(Dgetdrv(), DRIVE_C, "Dgetdrv still reports C:");

    /* A: is not there, so the request is ignored rather than obeyed */
    check(Dsetdrv(0), MAP_C, "Dsetdrv A: returns the drive map");
    check(Dgetdrv(), DRIVE_C, "Dsetdrv A: left the current drive alone");

    /* A path on a drive that does not exist must be refused, not turned into
     * a host path with the prefix still in it */
    check(Fcreate("A:\\DRVTEST", 0), E_DRIVE, "Fcreate on A: fails with EDRIVE");
    check(Fopen("A:\\DRVTEST", 0), E_DRIVE, "Fopen on A: fails with EDRIVE");
    check(Dcreate("A:\\DRVTEST"), E_DRIVE, "Dcreate on A: fails with EDRIVE");

    /* Naming the drive a file is on refers to the same file as leaving the
     * prefix out, both are relative to the current directory */
    h = Fcreate("C:DRVTEST", 0);
    check(h >= 0, 1, "Fcreate on C: succeeds");
    if (h >= 0)
        Fclose(h);
    check(Fdelete("DRVTEST"), 0, "Fdelete without a drive prefix succeeds");

    /* A path starting at the root of the drive names the same file as the
     * relative one. This is what lets an application build a path out of the
     * current directory Dgetpath handed it. */
    check(Dgetpath(path, 0), 0, "Dgetpath reports the current directory");
    strcat(path, "\\DRVTEST");

    h = Fcreate("DRVTEST", 0);
    check(h >= 0, 1, "Fcreate without a drive prefix succeeds");
    if (h >= 0)
        Fclose(h);
    check(Fdelete(path), 0, "Fdelete from the root of the drive succeeds");

    /*
     * The rest only holds when TOS_BASE_PATH has moved the root of the drive,
     * which the test suite runs this a second time to check. Everything above
     * has to be true either way, and is what says the two agree.
     *
     * The drive then stops somewhere, and walking up out of it has to fail
     * rather than reach the host file system above. Without a base path there
     * is nothing above C: to reach, so the same call is legal and this is
     * skipped rather than reversed.
     */
    if (argc > 1 && strcmp(argv[1], "BASED") == 0)
    {
        check(Dgetpath(path, 0), 0, "Dgetpath at the root of a moved drive");
        check(path[0], 0, "which is where this is run, so it is empty");

        check(Fopen("\\..\\Makefile", 0) < 0, 1,
              "a path leading out of the drive is refused");
        check(Fopen("..\\Makefile", 0) < 0, 1,
              "and so is a relative one");
    }

    printf("1..%d\n", n);

    return fails;
}
