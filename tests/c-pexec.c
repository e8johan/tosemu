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

/* Running a program from a program.
 *
 * tosemu forks for a Pexec and lets the child rebuild the emulated machine
 * around the program it was asked to run, so what a child inherits is what a
 * host process inherits. These are the assertions that has to keep.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>

#define E_FILNF     (-33)

/* MiNTLib binds these only in mint/sysbind.h, which cannot be included next to
 * osbind.h without redefining every trap macro, so bind them here */
#ifndef Pgetpid
# define Pgetpid()  (long)trap_1_w((short)0x10b)
# define Pgetppid() (long)trap_1_w((short)0x10c)
#endif

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

/* A command line as a basepage holds it: a length byte, the text, and a zero */
static void tail(char *buf, const char *text)
{
    int len = strlen(text);

    buf[0] = len;
    memcpy(buf+1, text, len);
    buf[len+1] = 0;
}

static char cmd[128];
static char buf[64];

int main(int argc, char **argv)
{
    long saved, h, r;

    /* A child's return value comes back whole. Pexec reports it with the high
     * word clear, only a failure of Pexec itself is negative. */
    tail(cmd, "");
    check(Pexec(0, "test-Pterm", cmd, 0L), 42, "Pexec reports what the child returned");

    /* A program that is not there is refused, and the caller carries on */
    check(Pexec(0, "NOSUCH.PRG", cmd, 0L), E_FILNF, "Pexec of a missing file fails with EFILNF");

    /* A child writes to the handle its parent redirected before starting it,
     * which is the whole point of an Fforce before a Pexec, and it finds the
     * command line it was handed in its basepage */
    saved = Fdup(1);
    h = Fcreate("PEXOUT", 0);
    Fforce(1, h);
    Fclose(h);

    tail(cmd, "12 345 6789");
    r = Pexec(0, "test-c-pexchild", cmd, 0L);

    Fforce(1, saved);
    Fclose(saved);

    check(r, 0x1234, "a return value wider than a host exit status survives");

    memset(buf, 0, sizeof buf);
    h = Fopen("PEXOUT", 0);
    if (h >= 0)
    {
        Fread(h, sizeof buf - 1, buf);
        Fclose(h);
    }
    check(strcmp(buf, "12 345 6789") == 0, 1, "the child wrote its command line to the redirected handle");
    Fdelete("PEXOUT");

    /* The parent is still itself once the child is gone */
    check(Fdup(1) >= 0, 1, "the parent's handles survive a Pexec");

    /* A process and the one that started it are told apart */
    check(Pgetpid() > 0, 1, "Pgetpid reports a process id");
    check(Pgetpid() != Pgetppid(), 1, "Pgetpid and Pgetppid differ");

    printf("# %d checks, %d failed\n", n, fails);

    return fails;
}
