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

/* Asking about GDOS, and asking it for fonts.
 *
 * vq_gdos is a knock on the door: the answer says whether anything above the
 * system fonts is there, and an application takes one path or the other from
 * it. vst_load_fonts is the first thing it calls if the answer was yes.
 *
 * That second call is the reason this file exists. EmuTOS implements it for
 * the interface real GDOS filled in rather than the one an application calls
 * through - it reads the head of the font chain out of control[10-11] - and
 * those two words are whatever the caller's array happened to hold. So a
 * program that asked for fonts on a machine that has none was reading a
 * pointer out of its own leftovers, and the count line at the end of this file
 * is what says it stopped doing so: an emulator that walked that chain never
 * reaches the end of main.
 */

#include <stdio.h>
#include <gem.h>

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

static short work_in[11];
static short work_out[57];
static short handle;

int main(void)
{
    short i;
    long version;

    /* Every attribute defaulted, and coordinates in pixels rather than in the
     * normalised space nothing has used since GEM was portable */
    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;

    v_opnwk(work_in, &handle, work_out);
    check(handle > 0, 1, "v_opnwk gives a workstation handle");

    /* The knock. vq_gdos answers whether anybody is there and vq_vgdos with
     * which of them it is, so the two have to agree: a machine with no GDOS
     * leaves the -2 the caller put in d0, and one with a GDOS puts something
     * else there. */
    version = vq_vgdos();
    check(vq_gdos() ? 1 : 0, version != GDOS_NONE,
          "vq_gdos and vq_vgdos agree about whether there is a GDOS");

    /* And the first thing an application does when told there is one. What is
     * being asked here is only that it answers - how many faces it should find
     * is stage three's question - because before this was fixed it did not
     * answer at all. */
    check(vst_load_fonts(handle, 0) >= 0, 1,
          "vst_load_fonts answers rather than walking a chain that is not there");

    /* A second time. The VDI loads fonts once and says so by answering with no
     * new faces, which is a different path through the same call: it leaves on
     * the first line rather than reaching the walk. */
    check(vst_load_fonts(handle, 0), 0,
          "asking a second time finds no faces that were not already there");

    vst_unload_fonts(handle, 0);
    check(vst_load_fonts(handle, 0) >= 0, 1,
          "and they can be asked for again once they have been unloaded");

    v_clswk(handle);

    printf("1..%d\n", n);

    return 0;
}
