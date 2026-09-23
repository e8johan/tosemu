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

#ifndef SHIFTER_H
#define SHIFTER_H

#include <stdint.h>

/*
 * The video shifter's registers: where the picture is read from, what shape
 * it is, and the colours it is shown in.
 *
 * The XBIOS screen calls are these registers seen from the other side.
 * Physbase reads the video base and Setscreen writes it, Setcolor and
 * Setpalette write the colours, and a program that goes past the XBIOS and
 * writes 0xFF8201 itself changes what Physbase says next. A debugger does it
 * that way - it keeps a screen of its own and swaps between that and the
 * program's by writing the base and the colours directly - so the two are one
 * piece of state rather than two that have to be kept in step.
 *
 * The shifter is an STE's, the one the VDI is told the machine has: the video
 * base has a low byte at 0xFF820D as well as the ST's two, and a colour has
 * four bits a gun. Registers with nothing behind them here - the STE's line
 * offset and horizontal scroll - hold what was written and do nothing with it.
 */

#define SHIFTER_BASE_ADDRESS (0xFF8200)
#define SHIFTER_LENGTH       (0x100)

/* The ST's sixteen colour registers, and the rest of the palette the XBIOS
 * extension calls reach, which is the TT's and the Falcon's */
#define SHIFTER_ST_COLOURS (16)
#define SHIFTER_COLOURS    (256)

/*
 * Points the video base at the screen a machine was built around, and says
 * what shape that screen is. Called whenever one is built, since each has its
 * own memory. The colours are left alone: they are the machine's rather than
 * the program's, and a program that runs another still has the colours it set.
 */
void shifter_init(uint32_t screen, int16_t planes);

uint32_t shifter_base(void);
void shifter_set_base(uint32_t base);

/* The ST resolution the shifter is in, 0 to 2, as the register holds it */
int16_t shifter_rez(void);

/*
 * Setting it, which is what Setscreen does and what a program writing 0xFF8260
 * itself does.
 *
 * Whether anything has set it is a separate question from what it holds, and
 * both are needed. The machine's own screen need not be a shape the register
 * can say - shifter_init describes a TT screen by its plane count alone - so a
 * mode of 2 means both "an ST high resolution screen" and "nobody has said
 * anything", and the two cannot be told apart from the value. What tells the
 * picture that the program has taken the video hardware over is that the
 * question was asked at all.
 */
void shifter_set_rez(int16_t rez);
int shifter_rez_set(void);

/*
 * A colour, 0x0RGB with four bits a gun. What is set is kept whole and what
 * is read of the first sixteen is only the twelve bits a register has, which
 * is what EmuTOS's Setcolor does too.
 */
uint16_t shifter_colour(int index);
void shifter_set_colour(int index, uint16_t colour);

/* A number that changes whenever a colour does, for something that keeps a
 * picture and wants to know whether it has to be shown again */
unsigned shifter_colour_changes(void);

struct _memarea;

uint8_t shifter_area_read(struct _memarea *area, uint32_t address);
void shifter_area_write(struct _memarea *area, uint32_t address,
                        uint8_t value);

#endif /* SHIFTER_H */
