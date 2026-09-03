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

/* Host replacement for EmuTOS's include/intmath.h.
 *
 * The originals are m68k muls/divs/divu with a wider intermediate than the
 * operands. C gives the same answers as long as the intermediate is written
 * out explicitly, which is what these do.
 */

#ifndef INTMATH_H
#define INTMATH_H

/* Integer square root. EmuTOS implements this in portable C in
 * util/intmath.c, which is compiled as it stands. */
ULONG Isqrt(ULONG x);

/* min and max live here in EmuTOS rather than in a header of their own, so
 * shadowing this one means providing them too. Copied as they stand: the
 * statement expression is what keeps the arguments from being evaluated twice
 * while still working for any type. */
#define min(a,b) \
({ \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a <= _b ? _a : _b; \
})

#define max(a,b) \
({ \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a >= _b ? _a : _b; \
})

/* return (m1 * m2) / d1, with a LONG intermediate */
static __inline__ WORD mul_div(WORD m1, WORD m2, WORD d1)
{
    return (WORD)(((LONG)m1 * (LONG)m2) / (LONG)d1);
}

/* returns (m1 * m2 + 32768) / 65536, with a ULONG intermediate */
static __inline__ UWORD umul_shift(UWORD m1, UWORD m2)
{
    return (UWORD)((((ULONG)m1 * (ULONG)m2) + 32768UL) >> 16);
}

static __inline__ LONG muls(WORD m1, WORD m2)
{
    return (LONG)m1 * (LONG)m2;
}

static __inline__ UWORD divu(ULONG d1, UWORD d2)
{
    return (UWORD)(d1 / d2);
}

#endif /* INTMATH_H */
