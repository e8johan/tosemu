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
 * The GEM trap.
 *
 * GEMDOS, BIOS and XBIOS take their arguments on the stack, so their handlers
 * read them from where a7 points. GEM does not. Both halves take a single
 * pointer in d1, to a block of pointers to the arrays that carry the arguments
 * and the answers, and put the function number inside one of those arrays
 * rather than in a register. d0 holds only which half of GEM is being asked.
 *
 * The arrays are the reason the two halves cannot share a dispatcher beyond
 * this file: an AES parameter block is six pointers and a VDI one is five, and
 * they do not describe the same things.
 */

#include "gem.h"

#include <stdio.h>

#include "gem_p.h"
#include "tossystem.h"
#include "surface.h"
#include "gfx.h"
#include "emuvdi/emuvdi.h"
#include "m68k.h"

/*
 * The screen, which both halves of GEM draw on and which neither owns.
 *
 * An ST low resolution screen, until there is somewhere for the choice to come
 * from. The VDI works out which resolution to report from how many planes this
 * has, so the two cannot disagree.
 */
#define SCREEN_WIDTH  (320)
#define SCREEN_HEIGHT (200)
#define SCREEN_PLANES (4)

static struct surface *screen;
static int started;

/*
 * Readies GEM, the first time anything asks for it.
 *
 * Either half can be the first to be called: an application that draws without
 * a window reaches the VDI first, and one that opens a window reaches the AES
 * first, so neither can be the one to set the other up.
 */
int gem_start()
{
    if (started)
        return 1;

    screen = surface_create(SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_PLANES);
    if (!screen)
    {
        halt_execution();
        printf("GEM: no room for a %dx%d screen\n", SCREEN_WIDTH, SCREEN_HEIGHT);
        return 0;
    }

    surface_select(screen);
    emuvdi_init();

    /*
     * A window to show it in, if there is a compositor to open one on. There
     * usually is not while a test is running, and the emulator is no different
     * for it: the screen is in memory either way, and this only decides
     * whether anyone can see it.
     */
    gfx_open(screen);

    started = 1;

    return 1;
}

void gem_trap()
{
    uint16_t which = m68k_get_reg(0, M68K_REG_D0) & 0xffff;

    switch (which)
    {
        case GEM_AES:
            aes_trap();
            break;
        case GEM_VDI:
            vdi_trap();
            break;
        default:
            halt_execution();
            printf("GEM called with 0x%x in d0, which is neither the AES (0x%x) "
                   "nor the VDI (0x%x)\n", which, GEM_AES, GEM_VDI);
            break;
    }
}

void gem_reset()
{
    aes_reset();
    vdi_reset();

    gfx_close();

    surface_free(screen);
    screen = 0;
    started = 0;
}

/* Parameter block arrays ***************************************************/

/*
 * An index outside the array the caller declared is a bug in this emulator
 * rather than in the application: the count comes from the control array the
 * application filled in, and a handler reading past it has misunderstood its
 * own function. Say so and read zero, which is a great deal easier to follow
 * than the arbitrary word that happened to be there.
 */
static int gem_in_range(uint32_t array, int count, int index)
{
    if (array == 0)
    {
        printf("GEM: parameter block array is a null pointer\n");
        return 0;
    }

    if (index < 0 || index >= count)
    {
        printf("GEM: index %d is outside the %d entry array at 0x%x\n",
               index, count, array);
        return 0;
    }

    return 1;
}

int16_t gem_word(uint32_t array, int count, int index)
{
    if (!gem_in_range(array, count, index))
        return 0;

    return (int16_t)m68k_read_memory_16(array + 2*index);
}

void gem_set_word(uint32_t array, int count, int index, int16_t value)
{
    if (!gem_in_range(array, count, index))
        return;

    m68k_write_memory_16(array + 2*index, (uint16_t)value);
}

uint32_t gem_long(uint32_t array, int count, int index)
{
    if (!gem_in_range(array, count, index))
        return 0;

    return m68k_read_memory_32(array + 4*index);
}

void gem_set_long(uint32_t array, int count, int index, uint32_t value)
{
    if (!gem_in_range(array, count, index))
        return;

    m68k_write_memory_32(array + 4*index, value);
}
