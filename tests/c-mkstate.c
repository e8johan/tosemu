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
 * Asking where the mouse is rather than waiting for it to go somewhere.
 *
 * These are two different questions with two different answers, and the whole
 * of this file is the difference between them. A wait is answered by a change,
 * and it has to be told the state as of that change - so the event loop reads
 * things as of the last one anybody took off the queue. graf_mkstate takes
 * nothing and waits for nothing, so answering it the same way tells it what
 * some earlier wait left behind and keeps telling it that for ever.
 *
 * Which is not a small wrongness. An application that tracks a slider does it
 * by asking in a loop, and an answer that never moves is a slider that never
 * moves and a loop that never ends: the button it is waiting to see released
 * was read before the person released it. ProCalc's About box is one.
 *
 * On an ST the two questions had one answer because there was no queue: the
 * keyboard processor kept the position and the buttons up to date, and reading
 * them cost nothing and consumed nothing. The last check here is the consumed
 * nothing half - polling must not eat what the waits are owed.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <gem.h>

/* Where the injected pointer goes, which the Makefile has to agree with */
#define LAST_X  (200)
#define LAST_Y  (100)

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

int main(int argc, char **argv)
{
    short mx = -1, my = -1, mb = -1, ks = -1;
    short again_x = -1, again_y = -1, again_b = -1, again_k = -1;
    short ev_x = 0, ev_y = 0, ev_b = 0, ev_k = 0, ev_clicks = 0;

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    /*
     * The pointer was moved twice and then pressed, and none of it has been
     * waited for. What it is now is where it ended up, not where it started.
     */
    check(graf_mkstate(&mx, &my, &mb, &ks), 1, "graf_mkstate answers");
    check(mx, LAST_X, "with the pointer where it got to");
    check(my, LAST_Y, "on both counts");
    check(mb, 1, "and the button as it is now, which is held down");

    /* Twice, because asking is not doing: an application in a loop asks over
     * and over and the answer must not wander */
    graf_mkstate(&again_x, &again_y, &again_b, &again_k);

    check(again_x, mx, "asking again says the same");
    check(again_y, my, "on both counts");
    check(again_b, mb, "and about the button");

    /*
     * And the press is still there to be waited for. Asking where the mouse is
     * must not take anything off the queue: the waits need every change and
     * not merely the latest, which is what stops a click too quick to be seen
     * from reading as a button that was never pressed.
     */
    ev_clicks = evnt_button(1, 1, 1, &ev_x, &ev_y, &ev_b, &ev_k);

    check(ev_clicks >= 1, 1, "waiting for the press afterwards still finds one");
    check(ev_x, LAST_X, "where it happened");
    check(ev_y, LAST_Y, "on both counts");

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
