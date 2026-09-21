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

/* Going into supervisor mode and coming back out of it.
 *
 * What this is really about is the stack. A 68000 keeps two stack pointers in
 * the one register and swaps them whenever the S bit moves, so a switch that
 * does no more than move the bit leaves the caller standing on the system's
 * stack rather than on its own - with none of what it had put there, and with
 * the address it is to return to somewhere it can no longer reach. Every check
 * below that compares two stack pointers is asking about that.
 *
 * Which is why nothing is said until the mode has been given back. A caller
 * whose stack has been taken away has a few words to spend before it runs off
 * the end of whatever it was handed, and printf spends far more than that: a
 * check reported from in there stops the emulator instead of failing, and a
 * test that stops says nothing about what it found. So the run gathers what it
 * sees, comes back out, and only then reports.
 */

#include <stdio.h>
#include <mint/osbind.h>

/* _bootdev, the drive the machine came up from. Any of the system variables
 * would do - what matters is that it is one, since the whole area they live in
 * is out of reach of a program running in user mode. */
#define BOOTDEV (0x446L)

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

/* Where the stack is standing. The value itself means nothing - it is a
 * different number in every frame - so every check below compares two of them
 * taken the same way rather than looking at one. */
static long stack_pointer(void)
{
    long sp;

    __asm__ volatile ("move.l %%sp,%0" : "=d" (sp));

    return sp;
}

/*
 * Whether the machine's own stack has room of its own.
 *
 * A routine run this way stands on the supervisor stack, and so does an
 * interrupt handler, and so does every trap. That stack used to start at 0x600
 * and grow downwards, which ran it straight into the system variables - 0x4BA
 * is the two hundred hertz counter and the whole area from 0x380 up is that
 * sort of thing. Nothing had noticed, because a Supexec'd routine is usually a
 * few instructions and every system variable was nought anyway, so writing
 * over them wrote nought onto nought.
 *
 * This is the same routine with the depth a handler has: one that saves its
 * registers and has somewhere to work has used a few hundred bytes before it
 * does anything at all. Three hundred and eighty four is past 0x4BA from where
 * the stack used to start and short of 0x380, which is the bottom of the area
 * that is mapped at all.
 */
#define HZ200 (0x4baL)
#define LIKE_A_HANDLER (384)

static long marker_survived;

static void deep(void)
{
    volatile unsigned long *counter = (volatile unsigned long *)HZ200;
    unsigned long was = *counter;
    volatile char scratch[LIKE_A_HANDLER];
    long sum = 0;
    int i;

    *counter = 0x5a5a5a5aUL;

    /* Written and read back, so that neither the writing nor the room it
     * needed can be decided against by the compiler */
    for (i = 0; i < LIKE_A_HANDLER; i++)
        scratch[i] = (char)i;
    for (i = 0; i < LIKE_A_HANDLER; i++)
        sum += scratch[i];

    marker_survived = (*counter == 0x5a5a5a5aUL) && (sum != 0);

    *counter = was;
}

/*
 * Going in with a supervisor stack of the caller's own, which is the other
 * thing Super(0) is not: the argument is the stack to stand on, and only
 * nought means the one the caller is on already. A debugger does it this way,
 * because it is about to hang handlers off the machine and wants them standing
 * somewhere a program being debugged cannot reach.
 *
 * In assembly rather than C because the stack changes under the code in the
 * middle, and compiled code finds its locals from the stack pointer. What it
 * sees goes into statics, and the user stack is put back by hand at the end:
 * the six bytes pushed for the first call are on it still, and the way out
 * carries four more across, so nothing is where the compiler left it.
 */
#define OTHER_STACK_LONGS (256)

static long other_stack[OTHER_STACK_LONGS];
static long stood_on, mode_there;

static void on_a_stack_of_its_own(void)
{
    static long ssp, usp;

    __asm__ volatile (
        "move.l  %%sp,%[usp]\n\t"
        "move.l  %[top],-(%%sp)\n\t"
        "move.w  #0x20,-(%%sp)\n\t"
        "trap    #1\n\t"
        "move.l  %%d0,%[ssp]\n\t"
        "move.l  %%sp,%[stood]\n\t"
        "pea     1.w\n\t"
        "move.w  #0x20,-(%%sp)\n\t"
        "trap    #1\n\t"
        "addq.l  #6,%%sp\n\t"
        "move.l  %%d0,%[mode]\n\t"
        "move.l  %[ssp],-(%%sp)\n\t"
        "move.w  #0x20,-(%%sp)\n\t"
        "trap    #1\n\t"
        "move.l  %[usp],%%sp\n\t"
        : [ssp] "+m" (ssp), [usp] "+m" (usp),
          [stood] "=m" (stood_on), [mode] "=m" (mode_there)
        : [top] "r" ((long)(other_stack + OTHER_STACK_LONGS))
        : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
}

int main(int argc, char **argv)
{
    long before, inside, after;
    long user_mode, super_mode, back_again, out_by_nought;
    long bootdev;
    long ssp;

    user_mode = Super(1L);

    before = stack_pointer();
    ssp = Super(0L);
    inside = stack_pointer();

    /* Read in here rather than reported from in here: this is the one check
     * that says the mode is a mode and not a flag somebody set, and a program
     * in user mode reading it stops the emulator */
    super_mode = Super(1L);
    bootdev = *(volatile unsigned char *)BOOTDEV;

    /* Whatever Super answered with is what puts things back, and putting them
     * back is the whole of what the answer is for */
    Super((void *)ssp);
    after = stack_pointer();
    back_again = Super(1L);

    check(user_mode, 0, "a program starts in user mode");
    check(inside, before, "Super leaves the caller standing on its own stack");
    check(super_mode, 1, "and in supervisor mode");
    check(bootdev == bootdev, 1, "so the system variables can be read");
    check(after, before, "coming back leaves it on its own stack as well");
    check(back_again, 0, "and in user mode again");

    on_a_stack_of_its_own();
    check(mode_there, super_mode,
          "Super with a stack of its own goes into supervisor mode too");
    check(stood_on, (long)(other_stack + OTHER_STACK_LONGS),
          "and stands the caller on that stack");
    check(Super(1L), 0, "and giving it back comes out again");

    /* And nought from supervisor mode is a way out rather than a way in:
     * which way Super goes is the mode's to say, not the argument's. Going
     * in again and giving back what the first call answered is what puts
     * the machine's own stack back where it was. */
    ssp = Super(0L);
    Super(0L);
    out_by_nought = Super(1L);
    Super(0L);
    Super((void *)ssp);
    check(out_by_nought, 0, "Super(0) from supervisor mode comes out of it");
    check(Super(1L), 0, "and giving the machine's stack back leaves it there");

    Supexec(deep);
    check(marker_survived, 1,
          "a routine with a handler's stack depth leaves the system variables alone");

    printf("# %d checks, %d failed\n", n, fails);
    printf("1..%d\n", n);

    return fails;
}
