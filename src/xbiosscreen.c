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
 * There are two jobs here. An application that draws needs somewhere to draw:
 * Physbase and Logbase hand out the block the machine reserved for a screen,
 * which is real, writable, and as large as the screen the machine has - so
 * that painting into it neither crashes nor corrupts anything, however much of
 * it the application paints. An application that configures the video
 * hardware needs its settings to hold: the palette and mode calls remember
 * what was set and report it back, so that code which sets a colour and reads
 * it again sees its own value rather than one it never chose.
 *
 * The physical screen and the colours are the shifter's registers - see
 * shifter.h - so a program that sets them through the XBIOS and one that
 * writes the registers are changing the same thing. Getrez reports the
 * resolution the screen really is, and a mode set here does not change it.
 */

#include "xbios.h"

#include <string.h>

#include "tossystem.h"
#include "console.h"
#include "cpu.h"
#include "m68k.h"
#include "gem_p.h"
#include "screen.h"
#include "shifter.h"
#include "surface.h"
#include "video.h"

#include "xbios_p.h"

#define PALETTE_ENTRIES (SHIFTER_COLOURS)

/* The physical screen is the shifter's video base; the logical one is where
 * TOS draws, which is only the XBIOS's own idea and has no register */
static int screen_asked;
static uint32_t screen_log;

/* The Falcon's colours, which are a different shape from the shifter's and
 * are only kept so that what is set reads back */
static uint32_t palette_rgb[PALETTE_ENTRIES];

static uint32_t video_mode;

void xbios_screen_reset()
{
    screen_asked = 0;
    screen_log = 0;
}

static void screen_buffer(void)
{
    if (!screen_asked)
    {
        screen_asked = 1;
        screen_log = shifter_base();
    }
}

int xbios_screen_named(uint32_t address)
{
    /* Nothing is the screen until something has asked where it is. Zero is
     * how an MFDB says "the screen" already, so answering yes to it before
     * then would make every bitmap the screen. */
    if (!screen_asked || !address)
        return 0;

    return address == shifter_base() || address == screen_log;
}

/*
 * The resolution, as the number the machine with this screen would have
 * answered.
 *
 * The screen GEM settled on once it has started, since a daemon can decide on
 * another one than this process asked for, and what the memory map was built
 * around before that. A shape no Atari had gets 8, which is no machine's
 * number: a program that looks the answer up in a table of the ones it knows
 * finds nothing there and says so, which is better than being told it has a
 * screen it has not.
 */
#define REZ_NO_MACHINE (8)

uint32_t XBIOS_Getrez()
{
    struct surface *screen = gem_screen_surface();
    int16_t width, height, planes, rez;

    FUNC_TRACE_ENTER

    if (screen)
    {
        width = (int16_t)surface_width(screen);
        height = (int16_t)surface_height(screen);
        planes = (int16_t)surface_planes(screen);
    }
    else
        screen_mode(&width, &height, &planes);

    rez = screen_rez(width, height, planes);

    return rez < 0 ? REZ_NO_MACHINE : (uint32_t)rez;
}

uint32_t XBIOS_Physbase()
{
    FUNC_TRACE_ENTER

    screen_buffer();

    return shifter_base();
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
    {
        screen_log = lscrn;
        tos_set_logical_screen(lscrn);
    }
    if (pscrn != 0xffffffff)
        shifter_set_base(pscrn);

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
    for (i = 0; i < SHIFTER_ST_COLOURS; ++i)
        shifter_set_colour(i, m68k_read_memory_16(palptr + 2*i));

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

    previous = shifter_colour(colornum);

    /* A negative mixture asks for the current colour without setting one */
    if (mixture >= 0)
        shifter_set_colour(colornum, (uint16_t)mixture);

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

    previous = shifter_colour(num);

    if (val >= 0)
        shifter_set_colour(num, (uint16_t)val);

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
        shifter_set_colour(start + i, m68k_read_memory_16(ptr + 2*i));

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
        m68k_write_memory_16(ptr + 2*i, shifter_colour(start + i));

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

    /* There is one buffer whatever the mode, being the screen this machine
     * has rather than one of the modes it was asked about, and this is how
     * big it is */
    return tos_screen_size();
}

/*
 * The end of a frame, which is what a program that draws its own picture says
 * when it has finished one - so it is where the picture is brought across.
 * It does not wait for the frame the way an ST did, which is what it always
 * did here.
 */
uint32_t XBIOS_Vsync()
{
    FUNC_TRACE_ENTER

    video_frame();

    return XBIOS_E_OK;
}

uint32_t XBIOS_Cursconf()
{
    uint16_t rate = peek_u16(2);
    uint16_t attr = peek_u16(4);

    FUNC_TRACE_ENTER_ARGS {
        printf("    rate: %d, attr: %d\n", rate, attr);
    }

    /*
     * This is the console's cursor rather than the screen's, so the console
     * answers it, http://toshyp.atari.org/en/00400a.html. There is one to show,
     * hide and blink now: it is the block on the console window, drawn by
     * inverting the cell it sits on.
     */
    return (uint32_t)(uint16_t)console_cursor((int16_t)rate, (int16_t)attr);
}
