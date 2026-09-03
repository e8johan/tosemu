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
 * How much memory the machine turned out to have.
 *
 * An Atari was sold in a handful of sizes and a program written for one of
 * them lays itself out from what it is given: how much there is decides how
 * large a document can be, and what happens when it runs out is a path through
 * the program that a machine with fifteen megabytes in it never takes.
 *
 * The size is named on the command line rather than compiled in, because the
 * point is that the answer follows what was asked for - the suite runs this
 * once for every size there is, and once more with the size said in a settings
 * file rather than in the environment.
 *
 * What is checked is that the size reached both halves of the machine. The
 * memory map is one: the screen has to sit at the top of the RAM there is and
 * every byte of the machine has to be memory that is really there. GEMDOS is
 * the other, and it is the half an application actually sees, because what
 * Malloc hands out is what the program has to live in - a map that was made
 * small while GEMDOS went on handing out addresses above it is the failure
 * this is for, and it is one an application would not survive.
 */

#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/falcon.h>
#include <mint/basepage.h>

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

/* Every number here is an address or an amount of memory, and what makes one
 * wrong is how far out it is, so a failure says which number it got and what
 * would have done rather than that something was not so */
static void check_between(long got, long low, long high, const char *name)
{
    n++;
    if (got >= low && got <= high)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got %ld, want between %ld and %ld)\n",
               n, name, got, low, high);
    }
}

/* The sizes there are, spelled as they are said. They are written out here
 * rather than asked for, so that a table that changed under the emulator is a
 * test that fails rather than a test that agrees with it. */
static const struct {
    const char *name;
    long bytes;
} sizes[] = {
    { "512k",   512L * 1024 },
    { "1m",    1024L * 1024 },
    { "2m",    2048L * 1024 },
    { "4m",    4096L * 1024 },
    { "14m",  14336L * 1024 },

    /* Not a machine: as much as the memory map has room for, which is
     * everything below the cartridge range. It is what a machine nobody said
     * anything about gets. */
    { "max",  0xFA0000L },
};

int main(int argc, char **argv)
{
    long top = 0, screen, screen_bytes, program, room, biggest;
    unsigned int i;

    if (argc < 2)
    {
        printf("Bail out! - no size named to expect\n");
        return 1;
    }

    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        if (strcmp(argv[1], sizes[i].name) == 0)
            top = sizes[i].bytes;

    if (top == 0)
    {
        printf("Bail out! - there is no size called %s\n", argv[1]);
        return 1;
    }

    /*
     * Where the screen is, which is what says where the RAM stops: it comes
     * off the top of the machine the way it did on an ST, where phystop was
     * the top and the screen sat below it. It is put on a 256 byte boundary,
     * which is where the hardware wanted one, so the slack above it is what
     * that rounding left over and nothing more.
     */
    screen = (long)Physbase();
    screen_bytes = VgetSize(0);

    check_between(screen + screen_bytes, top - 255, top,
                  "the screen ends at the top of the machine's memory");

    /* And the machine is memory the whole way up. The last byte is the one
     * worth writing to: an area that is a byte short of the top looks exactly
     * like one of the right size until something reaches the end of it. */
    *(char *)(top - 1) = 0x5a;
    check(*(char *)(top - 1) & 0xff, 0x5a,
          "the last byte of the machine is memory that is really there");

    *(char *)(screen - 1) = 0xa5;
    check(*(char *)(screen - 1) & 0xff, 0xa5,
          "and so is the byte below the screen, which is the top of the TPA");

    /* And how much of it this program was given, which for a program is all
     * of the machine below the screen: the block starts at the basepage and
     * ends where the screen begins */
    check((long)_base->p_hitpa, screen,
          "the program was given the machine up to the screen");

    /*
     * What GEMDOS makes of the same machine, which is the half an application
     * sees. The program handed back everything above itself before main was
     * reached - the startup code of every compiler does - so what is free is
     * the memory between the end of this program and the screen, less the
     * stack and the first of the heap that the C library kept out of it.
     *
     * The upper end of that is the bound that matters: memory handed out above
     * the screen would be memory the program shares with what it is drawing
     * on. The lower end is what says the size reached GEMDOS at all - an
     * allocator left with the top of some other machine answers with the
     * memory of that one, and answers it in the right shape.
     */
    program = (long)_base->p_bbase + _base->p_blen;
    room = screen - program;

    biggest = Malloc(-1L);
    check_between(biggest, room - 0x40000L, room,
                  "what is free is what lies between the program and the "
                  "screen");
    check(Malloc(top), 0,
          "and a block the size of the whole machine cannot be had");

    printf("1..%d\n", n);

    return fails;
}
