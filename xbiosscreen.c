/*
 * TOSEMU - an emulated environment for TOS applications
 * Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
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
 * Screen and video functions.
 *
 * tosemu shows nothing on a screen, so there are two jobs here. An application
 * that draws needs somewhere to draw: Physbase and Logbase hand out a real,
 * writable buffer, so that painting into it neither crashes nor corrupts
 * anything. An application that configures the video hardware needs its
 * settings to hold: the palette and mode calls remember what was set and
 * report it back, so that code which sets a colour and reads it again sees its
 * own value rather than one it never chose.
 *
 * What none of it does is take effect. Getrez deliberately reports a
 * resolution no ST has, so that code depending on the screen hardware fails
 * where it can be seen rather than drawing something nobody will look at, and
 * the rest of this file stays consistent with that choice.
 */

#include "xbios.h"

#include <string.h>

#include "tossystem.h"
#include "cpu.h"
#include "m68k.h"

#include "xbios_p.h"

/* A low resolution ST screen, 320x200 in 16 colours */
#define SCREENSIZE (32000)

#define PALETTE_ENTRIES (256)

static uint32_t screen_phys;
static uint32_t screen_log;

/* The ST palette holds 16 entries, the STE and Falcon extend it to 256. One
 * array covers all three, unset entries read back as black. */
static uint16_t palette[PALETTE_ENTRIES];
static uint32_t palette_rgb[PALETTE_ENTRIES];

static uint32_t video_mode;
static uint32_t cursor_rate = 10; /* Blinks per second, the TOS default */

static uint32_t screen_buffer(void)
{
    if (!screen_phys)
    {
        screen_phys = bios_static_alloc(SCREENSIZE);
        screen_log = screen_phys;
    }

    return screen_phys;
}

uint32_t XBIOS_Getrez()
{
    FUNC_TRACE_ENTER

    /* Custom value, to ensure that HW-dependent code fails */
    return 8;
}

uint32_t XBIOS_Physbase()
{
    FUNC_TRACE_ENTER

    screen_buffer();

    return screen_phys;
}

uint32_t XBIOS_Logbase()
{
    FUNC_TRACE_ENTER

    screen_buffer();

    return screen_log;
}

uint32_t XBIOS_Setscreen()
{
    uint32_t lscrn = peek_u32(2);
    uint32_t pscrn = peek_u32(6);
    int16_t rez = peek_s16(10);

    FUNC_TRACE_ENTER_ARGS {
        printf("    lscrn: 0x%x, pscrn: 0x%x, rez: %d\n", lscrn, pscrn, rez);
    }

    screen_buffer();

    /* -1 leaves that part of the setting alone */
    if (lscrn != 0xffffffff)
        screen_log = lscrn;
    if (pscrn != 0xffffffff)
        screen_phys = pscrn;

    /* The resolution is not ours to change, Getrez keeps its answer */

    return XBIOS_E_OK;
}

uint32_t XBIOS_Setpalette()
{
    uint32_t palptr = peek_u32(2);
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    palptr: 0x%x\n", palptr);
    }

    /* An ST palette is 16 words */
    for (i = 0; i < 16; ++i)
        palette[i] = m68k_read_memory_16(palptr + 2*i);

    return XBIOS_E_OK;
}

uint32_t XBIOS_Setcolor()
{
    uint16_t colornum = peek_u16(2);
    int16_t mixture = peek_s16(4);
    uint32_t previous;

    FUNC_TRACE_ENTER_ARGS {
        printf("    colornum: %d, mixture: 0x%x\n", colornum, mixture);
    }

    if (colornum >= PALETTE_ENTRIES)
        return XBIOS_E_OK;

    previous = palette[colornum];

    /* A negative mixture asks for the current colour without setting one */
    if (mixture >= 0)
        palette[colornum] = mixture;

    return previous;
}

uint32_t XBIOS_EsetColor()
{
    uint16_t num = peek_u16(2);
    int16_t val = peek_s16(4);
    uint32_t previous;

    FUNC_TRACE_ENTER_ARGS {
        printf("    num: %d, val: 0x%x\n", num, val);
    }

    if (num >= PALETTE_ENTRIES)
        return XBIOS_E_OK;

    previous = palette[num];

    if (val >= 0)
        palette[num] = val;

    return previous;
}

uint32_t XBIOS_EsetPalette()
{
    uint16_t start = peek_u16(2);
    uint16_t count = peek_u16(4);
    uint32_t ptr = peek_u32(6);
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    start: %d, count: %d, ptr: 0x%x\n", start, count, ptr);
    }

    for (i = 0; i < count && start + i < PALETTE_ENTRIES; ++i)
        palette[start + i] = m68k_read_memory_16(ptr + 2*i);

    return XBIOS_E_OK;
}

uint32_t XBIOS_EgetPalette()
{
    uint16_t start = peek_u16(2);
    uint16_t count = peek_u16(4);
    uint32_t ptr = peek_u32(6);
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    start: %d, count: %d, ptr: 0x%x\n", start, count, ptr);
    }

    for (i = 0; i < count && start + i < PALETTE_ENTRIES; ++i)
        m68k_write_memory_16(ptr + 2*i, palette[start + i]);

    return XBIOS_E_OK;
}

uint32_t XBIOS_VsetRGB()
{
    uint16_t index = peek_u16(2);
    uint16_t count = peek_u16(4);
    uint32_t array = peek_u32(6);
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    index: %d, count: %d, array: 0x%x\n", index, count, array);
    }

    for (i = 0; i < count && index + i < PALETTE_ENTRIES; ++i)
        palette_rgb[index + i] = m68k_read_memory_32(array + 4*i);

    return XBIOS_E_OK;
}

uint32_t XBIOS_VgetRGB()
{
    uint16_t index = peek_u16(2);
    uint16_t count = peek_u16(4);
    uint32_t array = peek_u32(6);
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    index: %d, count: %d, array: 0x%x\n", index, count, array);
    }

    for (i = 0; i < count && index + i < PALETTE_ENTRIES; ++i)
        m68k_write_memory_32(array + 4*i, palette_rgb[index + i]);

    return XBIOS_E_OK;
}

uint32_t XBIOS_VsetMode()
{
    int16_t mode = peek_s16(2);
    uint32_t previous = video_mode;

    FUNC_TRACE_ENTER_ARGS {
        printf("    mode: 0x%x\n", mode);
    }

    /* -1 asks which mode is set without setting one */
    if (mode >= 0)
        video_mode = mode;

    return previous;
}

uint32_t XBIOS_VgetSize()
{
    uint16_t mode = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    mode: 0x%x\n", mode);
    }

    /* There is one buffer whatever the mode, and this is how big it is */
    return SCREENSIZE;
}

uint32_t XBIOS_Cursconf()
{
    uint16_t rate = peek_u16(2);
    uint16_t attr = peek_u16(4);

    FUNC_TRACE_ENTER_ARGS {
        printf("    rate: %d, attr: %d\n", rate, attr);
    }

    /* There is no cursor to show, hide or blink, but the blink rate can be
     * asked for as well as set, http://toshyp.atari.org/en/00400a.html */
    switch (rate)
    {
    case 4: /* Set the blink rate */
        cursor_rate = attr;
        return XBIOS_E_OK;
    case 5: /* Report the blink rate */
        return cursor_rate;
    default:
        return XBIOS_E_OK;
    }
}
