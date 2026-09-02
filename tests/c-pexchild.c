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

/* The program c-pexec.c starts.
 *
 * It writes the command line it was handed to handle 1, which its parent has
 * pointed at a file, and leaves with a value too wide for the eight bits a
 * host exit status carries.
 */

#include <stdio.h>

int main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++)
        printf("%s%s", i > 1 ? " " : "", argv[i]);

    return 0x1234;
}
