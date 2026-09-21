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
 * A handler of the program's own on an OS trap.
 *
 * The OS calls are answered on the host without the trap going near its
 * vector, which is right until a program hangs something there: a debugger
 * watching for the program it is debugging to finish, a resident program
 * adding a call. Such a handler looks at the call, deals with the ones it
 * wants, and passes the rest on to whatever the vector held before it - so
 * what has to hold is that the handler is called, and that what it passes on
 * is answered as if it had not been there.
 *
 * The three ways handlers are written are all here. One passes everything on
 * and only counts. One answers a call itself and never passes it on. And one
 * calls through to the system as a subroutine, with a frame of its own and a
 * copy of the call on its own stack, so that it can change the answer on the
 * way back - which is the one that relies on the system reading the
 * arguments from wherever the frame's status register says the caller was.
 *
 * Nothing is printed while a handler is on the GEMDOS vector, since printing
 * is a GEMDOS call and would be counted.
 */

#include <stdio.h>
#include <mint/osbind.h>

/* What each vector held before, which is what the handlers pass calls on to,
 * and how many calls each has seen. Not static, because the handlers below are
 * assembly and find them by name. */
long gemdos_before, gem_before, bios_before, xbios_before;
long gemdos_calls, gem_calls, bios_calls, xbios_calls;

/*
 * The counting handlers, one for each trap. Each passes the call on by
 * pushing where it is to go and returning to it, with the exception frame
 * still on the stack as it arrived - which is how a handler that wants nothing
 * back from the call chains.
 */
#define COUNTING(name, calls, before)            \
    "       .globl  _" name "\n"                 \
    "_" name ":\n"                               \
    "       addq.l  #1,_" calls "\n"             \
    "       move.l  _" before ",-(%sp)\n"        \
    "       rts\n"

__asm__(
"       .text\n"
"       .even\n"
COUNTING("count_gemdos", "gemdos_calls", "gemdos_before")
COUNTING("count_gem", "gem_calls", "gem_before")
COUNTING("count_bios", "bios_calls", "bios_before")
COUNTING("count_xbios", "xbios_calls", "xbios_before")

/*
 * Answers Dgetdrv itself, with a drive nobody has, and passes the rest on.
 * The call is found where TOS finds it: on the user stack for a caller in
 * user mode, and just past the frame for one in supervisor mode.
 */
"       .globl  _answer_dgetdrv\n"
"_answer_dgetdrv:\n"
"       move.l  %usp,%a0\n"
"       btst    #5,(%sp)\n"
"       beq.s   1f\n"
"       lea     6(%sp),%a0\n"
"1:     cmp.w   #0x19,(%a0)\n"
"       bne.s   2f\n"
"       moveq   #7,%d0\n"
"       rte\n"
"2:     move.l  _gemdos_before,-(%sp)\n"
"       rts\n"

/*
 * Calls through for Sversion and adds one to what comes back. The call is
 * copied onto this stack and a frame is made in front of it that says
 * supervisor mode, so the system reads the copy and comes back here rather
 * than to the caller.
 */
"       .globl  _wrap_sversion\n"
"_wrap_sversion:\n"
"       move.l  %usp,%a0\n"
"       btst    #5,(%sp)\n"
"       beq.s   1f\n"
"       lea     6(%sp),%a0\n"
"1:     cmp.w   #0x30,(%a0)\n"
"       bne.s   3f\n"
"       move.w  #0x30,-(%sp)\n"
"       pea     2f\n"
"       move.w  %sr,-(%sp)\n"
"       move.l  _gemdos_before,-(%sp)\n"
"       rts\n"
"2:     addq.l  #2,%sp\n"
"       addq.l  #1,%d0\n"
"       rte\n"
"3:     move.l  _gemdos_before,-(%sp)\n"
"       rts\n");

extern void count_gemdos(void), count_gem(void), count_bios(void),
            count_xbios(void), answer_dgetdrv(void), wrap_sversion(void);

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

static long install(int vector, void (*handler)(void))
{
    return (long)Setexc(vector, (void (*)())handler);
}

static void uninstall(int vector, long before)
{
    Setexc(vector, (void (*)())before);
}

/* vq_gdos, which is a GEM call that needs no application: -2 in d0 and
 * trap #2, and -2 comes back when there is no GDOS */
static long gdos(void)
{
    register long d0 __asm__("d0") = -2;

    __asm__ volatile ("trap #2" : "+d" (d0) : : "d1", "d2", "a0", "a1", "a2",
                      "memory", "cc");

    return d0;
}

int main(void)
{
    long drv, version, largest, tickcal, rez, gdos_answer;
    long got_drv, got_version, got_largest, got_tickcal, got_rez, got_gdos;
    long calls_while_on, calls_after_off;
    long answered, passed, wrapped, wrapped_other;
    long ssp, super_drv, super_version, super_calls, super_mode_after;

    /* What the system answers with nobody in the way */
    drv = Dgetdrv();
    version = Sversion();
    largest = (long)Malloc(-1L);
    tickcal = Tickcal();
    rez = Getrez();
    gdos_answer = gdos();

    /* Passed on, all four traps. Setexc is a BIOS call itself, so the BIOS
     * handler goes on last and comes off first, and sees only Tickcal. */
    gemdos_before = install(VEC_GEMDOS, count_gemdos);
    gem_before = install(VEC_GEM, count_gem);
    xbios_before = install(VEC_XBIOS, count_xbios);
    bios_before = install(VEC_BIOS, count_bios);

    got_drv = Dgetdrv();
    got_version = Sversion();
    got_largest = (long)Malloc(-1L);
    got_tickcal = Tickcal();
    got_rez = Getrez();
    got_gdos = gdos();

    calls_while_on = gemdos_calls;
    uninstall(VEC_BIOS, bios_before);
    uninstall(VEC_XBIOS, xbios_before);
    uninstall(VEC_GEM, gem_before);
    uninstall(VEC_GEMDOS, gemdos_before);

    Dgetdrv();
    calls_after_off = gemdos_calls;

    check(got_drv, drv, "a GEMDOS call passed on is answered as before");
    check(got_version, version, "and so is another");
    check(got_largest, largest, "and one with an argument");
    check(calls_while_on, 3, "and the handler saw each of them");
    check(got_tickcal, tickcal, "a BIOS call passed on is answered as before");
    check(bios_calls, 2, "and its handler saw it, and the Setexc taking it off");
    check(got_rez, rez, "an XBIOS call passed on is answered as before");
    check(xbios_calls, 1, "and its handler saw it");
    check(got_gdos, gdos_answer, "a GEM call passed on is answered as before");
    check(gem_calls, 1, "and its handler saw it");
    check(calls_after_off, calls_while_on,
          "taking a handler off takes it out of the way");

    /* A handler that answers a call itself */
    gemdos_before = install(VEC_GEMDOS, answer_dgetdrv);
    answered = Dgetdrv();
    passed = Sversion();
    uninstall(VEC_GEMDOS, gemdos_before);

    check(answered, 7, "a call a handler answers itself is answered by it");
    check(passed, version, "and the ones it passes on still reach the system");

    /* A handler that calls through and changes the answer */
    gemdos_before = install(VEC_GEMDOS, wrap_sversion);
    wrapped = Sversion();
    wrapped_other = Dgetdrv();
    uninstall(VEC_GEMDOS, gemdos_before);

    check(wrapped, version + 1,
          "a handler calling through with a frame of its own gets the answer");
    check(wrapped_other, drv, "and passes the others on untouched");

    /* And calls from supervisor mode, whose arguments are on the supervisor
     * stack rather than the user one. Super goes through the handler too. */
    gemdos_calls = 0;
    gemdos_before = install(VEC_GEMDOS, count_gemdos);
    ssp = Super(0L);
    super_drv = Dgetdrv();
    super_version = Sversion();
    Super((void *)ssp);
    super_mode_after = Super(1L);
    super_calls = gemdos_calls;
    uninstall(VEC_GEMDOS, gemdos_before);

    check(super_drv, drv, "a call from supervisor mode is passed on");
    check(super_version, version, "and so is another");
    check(super_calls, 5, "and Super itself went through the handler");
    check(super_mode_after, 0, "and still came back out of supervisor mode");

    printf("1..%d\n", n);

    return fails;
}
