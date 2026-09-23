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

/* See shifter.h for what this is. */

#include "shifter.h"

/* For saying that a colour changed, which changes every picture there is */
#include "emuvdi/emuvdi.h"

/* The registers, as offsets from SHIFTER_BASE_ADDRESS - EmuTOS's
 * bios/screen.h names them */
#define REG_BASE_HI      (0x01)
#define REG_BASE_MID     (0x03)
#define REG_COUNT_HI     (0x05)
#define REG_COUNT_MID    (0x07)
#define REG_COUNT_LO     (0x09)
#define REG_SYNC         (0x0A)
#define REG_BASE_LO      (0x0D)
#define REG_PALETTE      (0x40)
#define REG_PALETTE_END  (REG_PALETTE + 2 * SHIFTER_ST_COLOURS)
#define REG_REZ          (0x60)

/* Four bits a gun, which is what an STE's colour register holds */
#define COLOUR_BITS (0x0fff)

/* Fifty hertz and the internal clock, which is what a European ST came up in */
#define SYNC_50HZ (0x02)

static uint32_t base;
static uint8_t rez;
static int rez_set;
static uint8_t sync_mode = SYNC_50HZ;
static uint16_t palette[SHIFTER_COLOURS];
static unsigned changes;
static int powered_up;

/* Everything written to an address with nothing behind it, kept so that it
 * reads back */
static uint8_t other[SHIFTER_LENGTH];

/*
 * The colours TOS set when the machine was switched on, which are EmuTOS's
 * dflt_palette in bios/screen.c.
 */
static const uint16_t power_up[SHIFTER_ST_COLOURS] = {
    0x0fff, 0x0f00, 0x00f0, 0x0ff0, 0x000f, 0x0f0f, 0x00ff, 0x0555,
    0x0333, 0x0f33, 0x03f3, 0x0ff3, 0x033f, 0x0f3f, 0x03ff, 0x0000
};

void shifter_init(uint32_t screen, int16_t planes)
{
    int i;

    base = screen;

    /* The ST's register can only say one of its own three, so a screen of
     * some other shape is described by how many planes it has - the one
     * thing the shifter's mode decides that a program reading it acts on */
    rez = (planes >= 4) ? 0 : ((planes == 2) ? 1 : 2);

    /* Which is the machine saying what it is rather than a program asking for
     * something, so it leaves the mode unset - see shifter_set_rez */
    rez_set = 0;

    if (powered_up)
        return;

    powered_up = 1;

    for (i = 0; i < SHIFTER_ST_COLOURS; i++)
        palette[i] = power_up[i];

    /*
     * And the one colour that is the ink, made black: on the two screens with
     * fewer than sixteen colours the last one there is is what text is drawn
     * in, and the table has red and yellow there. This is fixup_ste_palette.
     */
    if (rez == 1)
        palette[3] = palette[15];
    else if (rez == 2)
        palette[1] = palette[15];
}

uint32_t shifter_base(void)
{
    return base;
}

void shifter_set_base(uint32_t address)
{
    /* Twenty four bits, on a word: the low byte's lowest bit is not there */
    base = address & 0xfffffe;
}

int16_t shifter_rez(void)
{
    return rez;
}

void shifter_set_rez(int16_t mode)
{
    rez = (uint8_t)(mode & 0x03);
    rez_set = 1;
}

int shifter_rez_set(void)
{
    return rez_set;
}

uint16_t shifter_colour(int index)
{
    if (index < 0 || index >= SHIFTER_COLOURS)
        return 0;

    return index < SHIFTER_ST_COLOURS ? (palette[index] & COLOUR_BITS)
                                      : palette[index];
}

/*
 * A colour changing, which is every picture changing without anything having
 * been drawn: every pixel already holding that pen is a different colour now,
 * in every window. Said only when it did change, since a debugger writes the
 * same colours back every time it swaps screens.
 */
void shifter_set_colour(int index, uint16_t colour)
{
    if (index < 0 || index >= SHIFTER_COLOURS || palette[index] == colour)
        return;

    palette[index] = colour;
    changes++;
    host_palette_changed();
}

unsigned shifter_colour_changes(void)
{
    return changes;
}

uint8_t shifter_area_read(struct _memarea *area, uint32_t address)
{
    uint32_t offset = address - SHIFTER_BASE_ADDRESS;

    (void)area;

    if (offset >= REG_PALETTE && offset < REG_PALETTE_END)
    {
        uint16_t colour = shifter_colour((int)(offset - REG_PALETTE) / 2);

        return (offset & 1) ? (uint8_t)colour : (uint8_t)(colour >> 8);
    }

    switch (offset)
    {
    case REG_BASE_HI:
        return (uint8_t)(base >> 16);
    case REG_BASE_MID:
        return (uint8_t)(base >> 8);
    case REG_BASE_LO:
        return (uint8_t)base;

    /*
     * Where the shifter has got to in the picture, which on a real machine
     * runs from the base to the end of the screen once a frame. Nothing here
     * is drawing a frame a line at a time, so it is always at the start of
     * one - which is also what a program reading it straight after a
     * vertical blank would find.
     */
    case REG_COUNT_HI:
        return (uint8_t)(base >> 16);
    case REG_COUNT_MID:
        return (uint8_t)(base >> 8);
    case REG_COUNT_LO:
        return (uint8_t)base;

    case REG_SYNC:
        return sync_mode;
    case REG_REZ:
        return rez;
    default:
        return other[offset];
    }
}

void shifter_area_write(struct _memarea *area, uint32_t address,
                        uint8_t value)
{
    uint32_t offset = address - SHIFTER_BASE_ADDRESS;

    (void)area;

    if (offset >= REG_PALETTE && offset < REG_PALETTE_END)
    {
        int index = (int)(offset - REG_PALETTE) / 2;

        if (offset & 1)
            shifter_set_colour(index, (uint16_t)((palette[index] & 0xff00)
                                                 | value));
        else
            shifter_set_colour(index, (uint16_t)((palette[index] & 0x00ff)
                                                 | ((uint16_t)value << 8)));
        return;
    }

    switch (offset)
    {
    /*
     * An STE clears the low byte whenever the high or the middle one is
     * written, so that a program written for an ST - which sets only those
     * two - gets the screen on the 256 byte boundary it asked for rather than
     * one that still has an earlier program's low byte on it.
     */
    case REG_BASE_HI:
        base = ((uint32_t)value << 16) | (base & 0x00ff00);
        break;
    case REG_BASE_MID:
        base = ((uint32_t)value << 8) | (base & 0xff0000);
        break;
    case REG_BASE_LO:
        base = (base & 0xffff00) | (value & 0xfe);
        break;

    /* The counter is the shifter's own, and a write to it goes nowhere */
    case REG_COUNT_HI:
    case REG_COUNT_MID:
    case REG_COUNT_LO:
        break;

    case REG_SYNC:
        sync_mode = value;
        break;
    case REG_REZ:
        shifter_set_rez((int16_t)(value & 0x03));
        break;
    default:
        other[offset] = value;
        break;
    }
}
