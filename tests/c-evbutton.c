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
 * A wait for the buttons not to be some way, which is what bit 8 of the
 * clicks word asks for: 0x101 with mask 3 and state 0 is "one click, either
 * button down". EWeb waits for its clicks like that, and read as "both up" it
 * was answered at once with no button anywhere near it, wherever the pointer
 * was - so a link was followed by hovering over it.
 *
 * Run twice: with nothing pressed, and with "held", where TOSEMU_CLICKS
 * presses the left button at 200,100 and keeps it down.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>

static int n;
static int fails;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
    }
}

static short wait_buttons(short clicks, short mask, short state, short *mx, short *my,
                          short *mb)
{
    short msg[8], ks, kr, br;

    return evnt_multi(MU_BUTTON | MU_TIMER, clicks, mask, state,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, msg, 100, mx, my, mb, &ks, &kr, &br);
}

int main(int argc, char **argv)
{
    short mx, my, mb;
    int held = argc > 1 && strcmp(argv[1], "held") == 0;

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    if (!held)
    {
        check(wait_buttons(0x101, 3, 0, &mx, &my, &mb), MU_TIMER,
              "with nothing pressed, a wait for either button down runs out");
        check(wait_buttons(1, 3, 0, &mx, &my, &mb), MU_BUTTON,
              "and one for both up is answered at once, as before");
    }
    else
    {
        check(wait_buttons(0x101, 3, 0, &mx, &my, &mb), MU_BUTTON,
              "a press answers a wait for either button down");
        check(mb, 1, "with the left button");
        check(mx == 200 && my == 100, 1, "where it was pressed");
        check(wait_buttons(0x101, 3, 0, &mx, &my, &mb), MU_TIMER,
              "and answers it once, however long it is held");
    }

    appl_exit();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
