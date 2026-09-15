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
 * Whether the cartridge key answers what the chip answered.
 *
 * This is a transcription of sixteen boolean equations, and a transcription is
 * the one kind of code where the author reviewing it is worth nothing: an
 * inverted literal reads exactly like a correct one, and the only difference
 * it makes is to a bit stream a thousand clocks later. So none of what is
 * below was written by running the code and recording what came out. The
 * streams here were produced by an independent implementation of the same
 * device - the lookup table the SidecarTridge project built by a different
 * route, from the same fuse map - and this file exists to hold tosemu to them.
 *
 * Nothing of that project is copied here. Their tables are under a licence
 * this one could not take code from, and none was taken: what was used was the
 * answers, to check ours, which is what an independent implementation is for.
 * The questions below are our own so that the answers recorded are too.
 *
 * The one that matters most is not a stream at all but the very first check:
 * that a read is answered from what the chip was holding, and only then moves
 * it on. A registered output works that way and a combinatorial one does not,
 * and getting it the wrong way round gives a stream shifted by a single clock
 * - which agrees perfectly on a constant question and diverges on every other,
 * so it is a mistake that looks like working code until Cubase disagrees.
 */

#include <stdio.h>
#include <string.h>

#include "dongle.h"

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

static void check_stream(const char *questions, const char *answers,
                         const char *name)
{
    char got[256];
    size_t i;

    dongle_reset();

    for (i = 0; i < strlen(questions) && i < sizeof got - 1; i++)
    {
        got[i] = (char)('0' + dongle_answer());
        dongle_clock(questions[i] - '0');
    }

    got[i] = '\0';

    n++;

    if (strcmp(got, answers) == 0)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s\n", n, name);
        printf("#   asked %s\n", questions);
        printf("#   want  %s\n", answers);
        printf("#   got   %s\n", got);
    }
}

/*
 * The questions.
 *
 * Address bit 8 held low and held high are the two a person would try first
 * and say the least - a machine that ignored its input entirely would pass
 * both. Alternating is the smallest question that tells them apart. The last
 * is the bits of a piece of text, which is a source of a hundred-odd bits that
 * look nothing like the other three and that anybody can regenerate; what it
 * is really for is length, a hundred and four clocks being far past where a
 * single wrong literal could still be hiding.
 */
#define LOW   "0000000000000000000000000000000000000000000000000000000000000000"
#define HIGH  "1111111111111111111111111111111111111111111111111111111111111111"
#define ALT   "0101010101010101010101010101010101010101010101010101010101010101"

/* The bits of "tosemu dongle", most significant first */
#define TEXT \
    "01110100011011110111001101100101011011010111010100100000" \
    "01100100011011110110111001100111011011000110010"          \
    "1"

static void check_the_streams(void)
{
    check_stream(LOW,
                 "0000000000001111111111111111111111111111111111111111111111111111",
                 "asked the same question sixty four times, it answers this");
    check_stream(HIGH,
                 "0000000000000000000000000000000000000000000000000000000000000000",
                 "and the other question silences it entirely");
    check_stream(ALT,
                 "0000100010001000100010001000100010001000100010001000100010001000",
                 "asked alternately, it settles into one in four");
    check_stream(TEXT,
                 "00011110001000111110111100011110001000111001111011110000001000100010"
                 "001111100100001000100001110000100010",
                 "and a hundred and four questions of no pattern go like this");
}

/*
 * That the answer comes from before the clock rather than after it.
 *
 * Stated on its own because the streams above would catch it and not say what
 * it was: three of the four would fail together and the reason would have to
 * be worked out again. Held low, the chip answers nothing for twelve clocks
 * and then answers one for ever, so the boundary is at a countable place and
 * either side of it is a different sentence about when the register is read.
 */
static void check_when_the_register_is_read(void)
{
    int i;

    dongle_reset();

    check(dongle_answer(), 0, "a key just plugged in answers nothing");

    /* Eleven more clocks, which with the read above makes twelve reads - the
     * last of the twelve the stream held low says still answers nothing */
    for (i = 0; i < 11; i++)
        dongle_clock(0);

    check(dongle_answer(), 0, "and the twelfth read is still nothing");

    dongle_clock(0);

    check(dongle_answer(), 1, "the thirteenth read being the first to differ");
}

/* And that it goes back. A machine turned off and on again is the one thing a
 * state machine of this kind must be able to do, and tosemu asks for it every
 * time Pexec builds a machine a second time */
static void check_it_starts_again(void)
{
    int i;

    dongle_reset();

    for (i = 0; i < 40; i++)
        dongle_clock(i & 1);

    check(dongle_answer() == 0, 0, "wound forty clocks on, it is somewhere");

    dongle_reset();

    check(dongle_answer(), 0, "and a reset puts it back at the beginning");
}

/*
 * What the port reads as, byte by byte, which is where tosemu differs from a
 * bus and has to be made not to.
 */
static void check_the_port(void)
{
    uint8_t high, low;
    char one[80], other[80];
    int i;

    dongle_reset();

    high = dongle_area_read(0, CARTRIDGE_BASE_ADDRESS);
    check(high & 0xfe, 0xfe, "everything but bit 8 of a read is pulled high");

    low = dongle_area_read(0, CARTRIDGE_BASE_ADDRESS + 1);
    check(low, 0xff, "and the low half of the word carries nothing at all");

    /*
     * The two halves of a word are one access to the port and must move the
     * chip once. Reading both halves and reading only the high one therefore
     * have to give the same stream - if the low half clocked as well, this
     * would be that stream with every other bit missing.
     */
    dongle_reset();
    for (i = 0; i < 40; i++)
    {
        one[i] = (char)('0' + (dongle_area_read(0, CARTRIDGE_BASE_ADDRESS) & 1));
        dongle_area_read(0, CARTRIDGE_BASE_ADDRESS + 1);
    }
    one[i] = '\0';

    dongle_reset();
    for (i = 0; i < 40; i++)
        other[i] = (char)('0' + (dongle_area_read(0, CARTRIDGE_BASE_ADDRESS) & 1));
    other[i] = '\0';

    check(strcmp(one, other) == 0, 1,
          "a word read moves the key on once rather than twice");

    /*
     * And that the question really is address bit 8. Reading the two addresses
     * that differ only there has to be the same as asking low and asking high,
     * which is what the first two streams above already say the answers to.
     */
    dongle_reset();
    for (i = 0; i < 12; i++)
        dongle_area_read(0, CARTRIDGE_BASE_ADDRESS);
    check(dongle_area_read(0, CARTRIDGE_BASE_ADDRESS) & 1, 1,
          "twelve reads below bit 8 leave it answering one");

    dongle_reset();
    for (i = 0; i < 12; i++)
        dongle_area_read(0, CARTRIDGE_BASE_ADDRESS + 0x100);
    check(dongle_area_read(0, CARTRIDGE_BASE_ADDRESS + 0x100) & 1, 0,
          "and twelve reads above it leave it answering nothing");

    /* The port is a hundred and twenty eight kilobytes of the same chip: the
     * address decodes down to one bit and everything else about it is ignored,
     * so the far end of the port answers as the near end does */
    dongle_reset();
    for (i = 0; i < 12; i++)
        dongle_area_read(0, CARTRIDGE_BASE_ADDRESS + CARTRIDGE_LENGTH - 0x200);
    check(dongle_area_read(0, CARTRIDGE_BASE_ADDRESS + CARTRIDGE_LENGTH - 0x200) & 1,
          1, "the top of the port is the same chip as the bottom");
}

/* And that asking for a key that is not there is refused rather than silently
 * given this one, which would answer confidently and wrongly */
static void check_what_can_be_asked_for(void)
{
    check(dongle_asked_for("cubase"), 1, "there is a key called cubase");
    check(dongle_wanted(), 1, "and asking for it plugs it in");
    check(strcmp(dongle_named(), "cubase") == 0, 1, "and it says what it is");

    check(dongle_asked_for("cubase3"), 1, "which may also be spelled cubase3");

    printf("# the next line is the refusal, and is meant to be there\n");
    check(dongle_asked_for("black"), 0, "the key Cubase 2 came with is not this one");
    check(dongle_asked_for(""), 0, "and nothing named is nothing asked for");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    check_when_the_register_is_read();
    check_the_streams();
    check_it_starts_again();
    check_the_port();
    check_what_can_be_asked_for();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
