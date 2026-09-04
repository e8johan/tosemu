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
 * Asking what has happened without agreeing to wait for it.
 *
 * evnt_multi is how an application waits, and a timer of nought milliseconds
 * is how it asks the same question without waiting: the timer has already
 * expired, so everything else is looked at once and the answer comes straight
 * back as MU_TIMER. The AES does that in the quick checks at the top of
 * ev_multi, before it blocks for anything - see 3rdparty/emutos/aes/gemevlib.c.
 *
 * Reading the nought as "no timer at all" instead leaves the wait with only
 * the message queue to end it, and an application that polls stops dead at its
 * first poll with nothing on screen to say why. Atari Works is the case that
 * showed it: picking New from its opening dialog puts up the busy bee and
 * polls for the work it queued behind it, so the document window never
 * appeared and the menu bar never answered, the application never having got
 * back out of the poll to hear about either.
 *
 * A timer with a real interval on it still waits, and it is here so that
 * making the nought come back at once cannot be done by making every timer
 * come back at once.
 *
 * These run with no compositor, so nothing but the timer can ever end a wait.
 * A wait that blocks stops the emulator, which then prints nothing further -
 * so the count at the end is what says the whole file ran.
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

/* What a wait is asked for and told, which is the same every time here */
static short poll_for(unsigned long ms)
{
    short message[8];
    short mx = 0, my = 0, mb = 0, ks = 0, kr = 0, br = 0;

    return evnt_multi(MU_TIMER | MU_MESAG, 0, 0, 0,
                      0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0,
                      message, ms,
                      &mx, &my, &mb, &ks, &kr, &br);
}

int main(int argc, char **argv)
{
    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    /* The near neighbour, and the one that was already right: evnt_timer of
     * nought is not a wait either */
    evnt_timer(0);
    check(1, 1, "evnt_timer of no time at all comes back");

    check(poll_for(0), MU_TIMER,
          "a timer of no time at all has already expired");

    /*
     * Twice, because polling is what an application does in a loop. One poll
     * that answers and a second that blocks is the same hang arriving later.
     */
    check(poll_for(0), MU_TIMER, "and it answers the same the next time round");

    /* An interval is still an interval: this one has to wait for it */
    check(poll_for(200), MU_TIMER, "a timer with an interval on it still ends");

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
