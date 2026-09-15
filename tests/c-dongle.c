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
 * The cartridge key, asked from inside the machine.
 *
 * src/dongletest.c checks the equations and this checks the wiring, which are
 * different things and fail differently. The equations can be perfect and the
 * key still useless if the question is taken from the wrong address bit, or if
 * a word read moves it on twice because tosemu fetches a word as two bytes.
 * Neither of those is visible to a test that calls the chip directly, and both
 * are what a program reading the port would actually hit.
 *
 * So this asks the way a program asks - by reading the port - and it asks the
 * same hundred and four questions the host side asks, so that the two can be
 * compared. If this fails and the host side passes, nothing is wrong with the
 * key and something is wrong between it and the bus.
 *
 * It has to be the only thing that has read the port, because there is no way
 * to reset a key from inside the machine: it starts where the machine started
 * and every read since then has moved it. Nothing else in tosemu touches the
 * cartridge, so the first read below is the first read there has been.
 */

#include <stdio.h>
#include <string.h>

/*
 * The port, and the two addresses that differ only in bit 8 - which is the
 * question, that pin of the key being soldered to that line of the bus.
 */
#define PORT     (0xFB0000L)
#define ASK_LOW  (PORT)
#define ASK_HIGH (PORT + 0x100L)

/* The bits of "tosemu dongle", most significant first, and what the key
 * answers when it is asked them. Both are src/dongletest.c's, so that the two
 * sides of this are comparable */
#define QUESTIONS \
    "01110100011011110111001101100101011011010111010100100000" \
    "01100100011011110110111001100111011011000110010"          \
    "1"

#define ANSWERS \
    "00011110001000111110111100011110001000111001111011110000001000100010" \
    "001111100100001000100001110000100010"

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

int main(int argc, char **argv)
{
    char got[256];
    unsigned short first, second;
    size_t i;

    /*
     * What one read looks like before any of it is believed. Every line of the
     * bus but data bit 8 is pulled high, so a word off the port is 0xfeff or
     * 0xffff and nothing else - which also says the low half is carrying
     * nothing, and so has no business moving the key on.
     */
    first = *(volatile unsigned short *)PORT;
    check((first | 0x100) == 0xffff, 1, "a read off the port is all ones but bit 8");
    check(first & 0xff, 0xff, "and the half of it below bit 8 says nothing");

    /*
     * The questions, asked by reading one address or the other. A word each,
     * which is one access and must move the key on once - if the two halves of
     * a word each clocked it, the answers would be every other bit of these
     * and would go wrong on the second one.
     */
    got[0] = (char)('0' + ((first >> 8) & 1));

    for (i = 1; i < strlen(QUESTIONS) && i < sizeof got - 1; i++)
    {
        long where = (QUESTIONS[i] == '1') ? ASK_HIGH : ASK_LOW;

        second = *(volatile unsigned short *)where;
        got[i] = (char)('0' + ((second >> 8) & 1));
    }

    got[i] = '\0';

    n++;

    if (strcmp(got, ANSWERS) == 0)
        printf("ok %d - the key answers through the bus as it does on its own\n",
               n);
    else
    {
        fails++;
        printf("not ok %d - the key answers through the bus as it does on its own\n",
               n);
        printf("# want %s\n", ANSWERS);
        printf("# got  %s\n", got);
    }

    printf("1..%d\n", n);

    (void)argc;
    (void)argv;

    return fails;
}
