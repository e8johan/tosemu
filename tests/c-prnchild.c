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

/* A program that prints, Pexeced by another program that is printing.
 *
 * This is c-print's child and is not run on its own. Pexec forks, so the
 * child has a copy of everything the parent had open - including, when the
 * parent is in the middle of printing, a half written job and the name of the
 * temporary file it is being written to. Neither is the child's. A child that
 * finished the job it inherited would put a second copy of the parent's
 * document in the queue and take away the file the parent is still writing to,
 * and the parent's own document would never arrive.
 *
 * So this prints one page of its own. The parent prints two, and checks that
 * what came out has two pages in it: three would be this program's page on the
 * end of a document it never saw.
 *
 * It says nothing on stdout. The parent's output is a list of checks and a
 * count of them, and a line from here in the middle of it would be read as one
 * of the parent's.
 *
 * It opens no screen workstation either, which is the other half of what it is
 * for: a program that only prints need never have touched the screen, and the
 * printer's workstation is opened against the screen's, so something has to
 * notice that there is not one yet.
 */

#include <gem.h>
#include <mint/osbind.h>

static short work_in[11];
static short work_out[57];

int main(void)
{
    short handle, pxy[4];
    short i;

    for (i = 0; i < 10; i++)
        work_in[i] = 1;
    work_in[10] = 2;

    work_in[0] = 21;
    v_opnwk(work_in, &handle, work_out);

    if (handle <= 0)
        return 1;

    pxy[0] = 50; pxy[1] = 50;
    pxy[2] = 150; pxy[3] = 150;

    vsf_interior(handle, FIS_SOLID);
    vsf_color(handle, 1);
    v_bar(handle, pxy);

    v_updwk(handle);
    v_clswk(handle);

    return 0;
}
