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

/* Host replacement for EmuTOS's include/asm.h.
 *
 * EmuTOS's version is m68k inline assembly. Every macro in it has a portable
 * meaning, so this provides the same eleven names in C. It goes first on the
 * include path, so the VDI sources reach this rather than EmuTOS's without
 * being edited.
 */

#ifndef ASM_H
#define ASM_H

/* A routine that does nothing but return, which EmuTOS points unused vectors
 * at. It is assembly there only because a vector has to point at something. */
extern void just_rts(void);

/* Rotate a WORD by one bit, in place */
#define rolw1(x)    (x) = (UWORD)(((UWORD)(x) >> 15) | ((UWORD)(x) << 1))
#define rorw1(x)    (x) = (UWORD)(((UWORD)(x) >> 1) | ((UWORD)(x) << 15))

/* Rotate a LONG by count bits, in place */
#define roll(x, count) \
    (x) = (ULONG)(((ULONG)(x) << ((count) & 31)) | ((ULONG)(x) >> ((32 - ((count) & 31)) & 31)))
#define rorl(x, count) \
    (x) = (ULONG)(((ULONG)(x) >> ((count) & 31)) | ((ULONG)(x) << ((32 - ((count) & 31)) & 31)))

/* Swap the two bytes of a WORD, and the two words of a LONG, in place */
#define swpw(a)     (a) = (UWORD)((((UWORD)(a) & 0xff) << 8) | (((UWORD)(a) >> 8) & 0xff))
#define swpl(a)     (a) = (ULONG)((((ULONG)(a) & 0xffffUL) << 16) | (((ULONG)(a) >> 16) & 0xffffUL))

/* Swap the bytes within each half of a LONG, leaving the halves in place */
#define swpw2(a)    (a) = (ULONG)((((ULONG)(a) & 0x00ff00ffUL) << 8) | \
                                  (((ULONG)(a) >> 8) & 0x00ff00ffUL))

/*
 * The status register masks interrupts. Nothing interrupts a hosted VDI, so
 * the old value it reports is one that restoring will not act on.
 */
#define get_sr()    (0)
#define set_sr(a)   ((void)(a), 0)

/* A busy wait for hardware to settle, with no hardware to wait for */
#define delay_loop(count)   ((void)(count))

/*
 * EmuTOS declares this in asm.h and implements it in util/miscasm.S, so the
 * replacement belongs here rather than in intmath.h.
 *
 * It rounds by doubling before the divide and halving after, and the halving
 * is an arithmetic shift rather than a divide. The two differ on negative
 * numbers: a shift rounds towards minus infinity where a divide truncates
 * towards zero, which is the whole point of the plus or minus one above it.
 * gcc gives >> on a signed type the arithmetic meaning, which is what makes
 * this the same function rather than merely a similar one.
 */
static __inline__ WORD mul_div_round(WORD mult1, WORD mult2, WORD divisor)
{
    LONG q = ((LONG)mult1 * (LONG)mult2 * 2) / (LONG)divisor;

    q += (q < 0) ? -1 : 1;

    return (WORD)(q >> 1);
}

#endif /* ASM_H */
