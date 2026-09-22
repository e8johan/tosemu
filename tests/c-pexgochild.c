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

/* The program c-pexgo.c runs with Pexec modes 4 and 6.
 *
 * It is told what to do and where its caller's report is on its command line,
 * and writes what it found straight into the caller's memory - which only
 * works if it is running in the caller's machine.
 */

#include <stdlib.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/basepage.h>

/* The same as in c-pexgo.c */
struct report {
    long base;
    long parent;
    long dta;
    long block;
    long handle;
    long grandchild;
};

static char dta[44];

int main(int argc, char **argv)
{
    struct report *r;

    if (argc < 3)
        return 1;

    r = (struct report *)strtoul(argv[2], NULL, 16);

    r->base = (long)_base;
    r->parent = (long)_base->p_parent;
    r->dta = (long)Fgetdta();

    /* Everything a program has of its own, taken and not given back */
    if (strcmp(argv[1], "leave") == 0)
    {
        r->block = (long)Malloc(1024);
        r->handle = Fcreate("pexgo.out", 0);
        Fforce(1, (short)r->handle);
        Dsetpath("..");
        Fsetdta((_DTA *)dta);

        return 7;
    }

    /* A child of its own, run the same way */
    if (strcmp(argv[1], "grandchild") == 0)
    {
        char cmd[2] = { 0, 0 };
        long bp = Pexec(3, "test-Pterm", cmd, NULL);

        r->grandchild = bp > 0 ? Pexec(4, NULL, (void *)bp, NULL) : bp;
        Mfree((void *)bp);

        return 9;
    }

    return 1;
}
