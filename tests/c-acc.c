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
 * An accessory that does not stop, for testing what stops it.
 *
 * Every other test here says what it found and prints a count. This one cannot:
 * an accessory has nothing to exit to, so it never reaches an end to report
 * from - and being ended is the whole of what is being checked. So it asserts
 * nothing itself. What it is for is to be a process that is still there, and
 * the check line in the Makefile is what looks to see whether it still is.
 *
 * Two things about it are deliberate and would each make the test pass whether
 * or not the daemon did its part.
 *
 * It waits on a timer as well as on a message. An accessory waiting only for a
 * message has nothing left that could ever send it one once the daemon has
 * gone, and the emulator says so and stops rather than sit there for ever -
 * which looks exactly like having been stopped by the daemon. Waiting with a
 * timer as well means there is always something to wait for, so the process
 * outlives the daemon unless something ends it.
 *
 * And it says nothing at all. Its output goes wherever the daemon's does,
 * because the daemon started it, and two processes writing to one file leave
 * neither of them readable.
 *
 * AP_TERM is ignored along with everything else, which is not laziness: a real
 * accessory has nothing to exit to either, and being asked nicely is the rung
 * of the ladder that a period accessory does not answer.
 */

#include <gem.h>

int main(void)
{
    short message[8];
    short id, ignored;

    id = appl_init();

    if (menu_register(id, "  Test  ") < 0)
        return 1;

    for (;;)
        evnt_multi(MU_MESAG | MU_TIMER,
                   0, 0, 0,
                   0, 0, 0, 0, 0,
                   0, 0, 0, 0, 0,
                   message, 100UL,
                   &ignored, &ignored, &ignored, &ignored, &ignored, &ignored);

    /* Not reached, and that is what makes it an accessory */
}
