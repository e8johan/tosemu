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

/* Running a program that has already been loaded, which is Pexec modes 4 and
 * 6.
 *
 * Unlike the other modes these run the program in the caller's machine, as
 * TOS did with the one address space it had. So what the child writes the
 * caller can read, a handler the caller has on an exception vector catches the
 * child, and what the child had of its own is gone or put back once it ends.
 * c-pexgochild.c is the child.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/basepage.h>

#define E_IHNDL     (-37)
#define E_IMBA      (-40)

#define ILLEGAL     (0x4afc)
#define VEC_ILLEGAL (4)

/* The fields of a basepage this reaches into */
#define BP_TBASE(p)   (*(long *)((char *)(p) + 0x08))

/* What the child writes back, the same as in c-pexgochild.c */
struct report {
    long base;
    long parent;
    long dta;
    long block;
    long handle;
    long grandchild;
};

static struct report report;

/*
 * Pexec mode 4 with a pattern in every register a caller keeps, counting the
 * ones that do not have it afterwards, and whether the stack came back where
 * it was. Answers what Pexec answered.
 *
 * Assembly because the compiler would otherwise decide what is in those
 * registers, and a register the compiler did not happen to be using could be
 * lost without anything noticing.
 */
long register_damage;
long sp_before;

#define KEEPS(reg, value)                   \
    "       cmp.l   #" value ",%" reg "\n"  \
    "       beq.s   1f\n"                   \
    "       addq.l  #1,%d1\n"               \
    "1:\n"

__asm__(
"       .text\n"
"       .even\n"
"       .globl  _pexec_keeping\n"
"_pexec_keeping:\n"
"       movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
"       move.l  48(%sp),%d0\n"
"       move.l  #0x22222222,%d2\n"
"       move.l  #0x33333333,%d3\n"
"       move.l  #0x44444444,%d4\n"
"       move.l  #0x55555555,%d5\n"
"       move.l  #0x66666666,%d6\n"
"       move.l  #0x77777777,%d7\n"
"       move.l  #0xa2a2a2a2,%a2\n"
"       move.l  #0xa3a3a3a3,%a3\n"
"       move.l  #0xa4a4a4a4,%a4\n"
"       move.l  #0xa5a5a5a5,%a5\n"
"       move.l  #0xa6a6a6a6,%a6\n"
"       clr.l   -(%sp)\n"
"       move.l  %d0,-(%sp)\n"
"       clr.l   -(%sp)\n"
"       move.w  #4,-(%sp)\n"
"       move.w  #0x4b,-(%sp)\n"
"       move.l  %sp,_sp_before\n"
"       trap    #1\n"
"       moveq   #0,%d1\n"
"       cmp.l   _sp_before,%sp\n"
"       beq.s   1f\n"
"       addq.l  #1,%d1\n"
"1:\n"
KEEPS("d2", "0x22222222")
KEEPS("d3", "0x33333333")
KEEPS("d4", "0x44444444")
KEEPS("d5", "0x55555555")
KEEPS("d6", "0x66666666")
KEEPS("d7", "0x77777777")
KEEPS("a2", "0xa2a2a2a2")
KEEPS("a3", "0xa3a3a3a3")
KEEPS("a4", "0xa4a4a4a4")
KEEPS("a5", "0xa5a5a5a5")
KEEPS("a6", "0xa6a6a6a6")
"       move.l  %d1,_register_damage\n"
"       lea     16(%sp),%sp\n"
"       movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
"       rts\n");

long pexec_keeping(long basepage);

/*
 * A handler for the illegal instruction vector, which is what a debugger puts
 * there: it notes where it was and in what mode, puts back the instruction the
 * ILLEGAL stood in for, and returns to it.
 */
long caught_pc;
short caught_sr;
short original;

__asm__(
"       .text\n"
"       .even\n"
"       .globl  _illegal_caught\n"
"_illegal_caught:\n"
"       move.l  %a0,-(%sp)\n"
"       move.w  4(%sp),_caught_sr\n"
"       move.l  6(%sp),_caught_pc\n"
"       move.l  6(%sp),%a0\n"
"       move.w  _original,(%a0)\n"
"       move.l  (%sp)+,%a0\n"
"       rte\n");

extern void illegal_caught(void);

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
static char text[64];
static char before[256];
static char after[256];

int main(int argc, char **argv)
{
    long bp, r, h, size, dta, block, handle, tbase, old;

    /* Said first, so that the buffer the C library prints through is taken
     * before any of the memory is handed to a child */
    printf("# Pexec modes 4 and 6, from 0x%lx\n", (long)_base);
    fflush(stdout);

    /* A child that takes everything a process has of its own and gives none
     * of it back. Looked at before anything is printed, so that nothing the
     * caller does in between can reuse what the child left. */
    Dgetpath(before, 0);
    dta = (long)Fgetdta();

    sprintf(text, "leave %lx", (long)&report);
    tail(cmd, text);
    bp = Pexec(PE_LOAD, "test-c-pexgochild", cmd, NULL);
    r = bp > 0 ? Pexec(PE_GO, NULL, (void *)bp, NULL) : bp;

    block = (long)Mfree((void *)report.block);
    handle = Fclose((short)report.handle);
    Dgetpath(after, 0);

    check(r, 7, "Pexec 4 answers with what the child returned");
    check(report.base, bp, "the child wrote into its caller's memory");
    check(report.parent, (long)_base, "the child's basepage names its caller");
    check(report.dta, bp + 0x80, "the child's DTA started in its own basepage");
    check(block, E_IMBA, "what the child allocated went when it ended");
    check(handle, E_IHNDL, "what the child opened was closed when it ended");
    check(strcmp(before, after), 0, "the caller is standing where it was");
    check((long)Fgetdta(), dta, "the caller has its DTA back");
    check(Mfree((void *)bp), 0, "mode 4 leaves the program's memory with the caller");

    /* The child forced its standard output onto the file it made. What this
     * prints has to reach the caller's own, and nothing of it the file. */
    fflush(stdout);
    h = Fopen("pexgo.out", 0);
    size = h >= 0 ? Fseek(0, (short)h, 2) : h;
    if (h >= 0)
        Fclose((short)h);
    check(size, 0, "the caller's standard output is its own again");
    Fdelete("pexgo.out");

    /* A caller's registers and stack, as TOS keeps them across a call */
    tail(cmd, "");
    bp = Pexec(PE_LOAD, "test-Pterm", cmd, NULL);
    r = bp > 0 ? pexec_keeping(bp) : bp;
    check(r, 42, "Pexec 4 from assembly answers with what the child returned");
    check(register_damage, 0, "and the caller's registers and stack are as it left them");
    Mfree((void *)bp);

    /* A child that runs one of its own the same way */
    sprintf(text, "grandchild %lx", (long)&report);
    tail(cmd, text);
    bp = Pexec(PE_LOAD, "test-c-pexgochild", cmd, NULL);
    r = bp > 0 ? Pexec(PE_GO_FREE, NULL, (void *)bp, NULL) : bp;
    check(r, 9, "a child that ran a child ends with its own value");
    check(report.grandchild, 42, "having been handed back its child's");
    check(Mfree((void *)bp), E_IMBA, "mode 6 gave the memory back when the child ended");

    /* What a debugger does: a breakpoint on the child's first instruction,
     * caught by a handler of the caller's */
    tail(cmd, "");
    bp = Pexec(PE_LOAD, "test-Pterm", cmd, NULL);
    tbase = BP_TBASE(bp);
    original = *(short *)tbase;
    *(short *)tbase = ILLEGAL;

    old = (long)Setexc(VEC_ILLEGAL, illegal_caught);
    r = Pexec(PE_GO, NULL, (void *)bp, NULL);
    Setexc(VEC_ILLEGAL, (void (*)())old);

    check(caught_pc, tbase, "an ILLEGAL in the child stopped in its caller's handler");
    check(caught_sr & 0x2000, 0, "which found the child in user mode");
    check(r, 42, "and the child ran on to its end once the handler let it");
    Mfree((void *)bp);

    printf("# %d checks, %d failed\n", n, fails);
    printf("1..%d\n", n);

    return fails != 0;
}
