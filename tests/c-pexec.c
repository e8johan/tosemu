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
#define E_INVFN     (-32)

/* MiNTLib binds these only in mint/sysbind.h, which cannot be included next to
 * osbind.h without redefining every trap macro, so bind them here */
#ifndef Pgetpid
# define Pgetpid()  (long)trap_1_w((short)0x10b)
# define Pgetppid() (long)trap_1_w((short)0x10c)
#endif
#ifndef Pwait
# define Pwait()          (long)trap_1_w((short)0x109)
# define Pwaitpid(p,f,r)  \
    (long)trap_1_wwwll((short)0x13a,(short)(p),(short)(f),(long)(r),0L)
#endif

/* The Pexec modes, as named in mint/ostruct.h, which only declares the
 * asynchronous ones when it is building for MiNT */
#ifndef PE_ASYNC_LOADGO
# define PE_ASYNC_LOADGO (100)
#endif
#ifndef PE_ASYNC_GO
# define PE_ASYNC_GO     (104)
#endif

/* The fields of a basepage this reaches into */
#define BP_LOWTPA(p)  (*(long *)((char *)(p) + 0x00))
#define BP_HITPA(p)   (*(long *)((char *)(p) + 0x04))
#define BP_TBASE(p)   (*(long *)((char *)(p) + 0x08))
#define BP_TLEN(p)    (*(long *)((char *)(p) + 0x0c))
#define BP_ENV(p)     (*(long *)((char *)(p) + 0x2c))

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
    long saved, h, r, bp, w;

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

    /* Making room for a program without loading one. What comes back owns
     * everything that was free, for the caller to Mshrink back down. */
    tail(cmd, "");
    bp = Pexec(PE_CBASEPAGE, 0L, cmd, 0L);
    check(bp > 0, 1, "Pexec 5 makes a basepage");
    check(BP_LOWTPA(bp), bp, "the basepage says where its memory starts");
    check(BP_HITPA(bp) > bp, 1, "the basepage says where its memory ends");
    check(BP_TLEN(bp), 0, "there is no program behind it");
    check(BP_ENV(bp) != 0, 1, "it inherited an environment");
    check(Mfree(bp), 0, "the memory it was given goes back");

    /* An environment pointer of -1 asks for none at all */
    bp = Pexec(PE_CBASEPAGE, 0L, cmd, (void *)-1L);
    check(bp > 0, 1, "Pexec 5 with no environment makes a basepage");
    check(BP_ENV(bp), 0, "and that basepage names no environment");
    Mfree(bp);

    /* Loading a program without running it, then running it */
    bp = Pexec(PE_LOAD, "test-Pterm", cmd, 0L);
    check(bp > 0, 1, "Pexec 3 loads a program");
    check(BP_TBASE(bp), bp + 0x100, "the program sits above its basepage");
    check(BP_TLEN(bp) > 0, 1, "and something was loaded there");
    check(Pexec(PE_GO, 0L, (void *)bp, 0L), 42, "Pexec 4 runs it");
    check(Mfree(bp), 0, "its memory goes back afterwards");

    /* Mode 6 runs it and hands the memory back on its own */
    bp = Pexec(PE_LOAD, "test-Pterm", cmd, 0L);
    check(Pexec(PE_GO_FREE, 0L, (void *)bp, 0L), 42, "Pexec 6 runs it");
    check(Mfree(bp), -40, "and had already given the memory back");

    /* The modes that do not wait answer with a process id, and Pwait reports
     * that same one along with the value the child left with, moved up a byte */
    r = Pexec(PE_ASYNC_LOADGO, "test-Pterm", cmd, 0L);
    check(r > 0, 1, "Pexec 100 answers with a process id");
    w = Pwait();
    check((w >> 16) & 0xffff, r, "Pwait reports that process");
    check(w & 0xffff, 42 << 8, "and what it returned");

    bp = Pexec(PE_LOAD, "test-Pterm", cmd, 0L);
    r = Pexec(PE_ASYNC_GO, 0L, (void *)bp, 0L);
    check(r > 0, 1, "Pexec 104 answers with a process id");
    w = Pwaitpid((short)r, 0, 0L);
    check((w >> 16) & 0xffff, r, "Pwaitpid reports the one it was asked about");
    check(w & 0xffff, 42 << 8, "and what it returned");
    Mfree(bp);

    /* Nothing left to collect */
    check(Pwait(), E_FILNF, "Pwait with no children left fails with EFILNF");

    /* A mode that is not there is refused rather than guessed at */
    check(Pexec(42, 0L, cmd, 0L), E_INVFN, "an unknown Pexec mode fails with EINVFN");

    printf("# %d checks, %d failed\n", n, fails);

    return fails;
}
