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

/* Pexec mode 200, which replaces the running program with another one.
 *
 * There is no process in between, so this cannot report what happened: it is
 * gone either way. Running test-Pterm in its place is what makes the two
 * outcomes tell apart, 42 against the 1 below.
 */

#include <mint/osbind.h>

static char cmd[128];

int main(int argc, char **argv)
{
    cmd[0] = 0;
    cmd[1] = 0;

    Pexec(200, "test-Pterm", cmd, 0L);

    return 1;
}
