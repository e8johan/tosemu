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
 * The key Cubase wants plugged into the cartridge port.
 *
 * Steinberg shipped Cubase with a dongle, and Cubase will not start without
 * one. It is not a serial number or a file it looks for: it is a chip on the
 * ROM port that answers a question nobody can answer without the chip. The
 * program reads the port over and over, and the answers - one bit per read -
 * come out as a stream it recognises or does not.
 *
 * The chip is an Altera 5C060, a programmable logic device, and what it
 * contains is a sixteen bit registered state machine. Each of its sixteen
 * outputs is fed by a sum of products of the current outputs and one input,
 * and the new value is the old one exclusive-ored with that sum. One of the
 * sixteen, pin 22, is wired to data bit 8 and is the answer; the input, pin
 * 14, is wired to address bit 8 and is the question. Every access to the port
 * clocks it once. So the program asks by choosing which address it reads and
 * listens by looking at one bit of what comes back, and the sequence it gets
 * depends on every question it has asked since the machine was turned on.
 *
 * The equations below are those of the device, taken from the de-fused
 * contents of its fuse map - the file the period tools called SRD_OK.JED -
 * as published in the MiSTery Atari ST core's cubase3_dongle.v, whose header
 * records where they came from and reserves no rights in them. They are the
 * hardware written down rather than anybody's program, which is why they can
 * be here at all: the other reconstruction of this device in circulation is
 * under a licence this project could not take code from without changing its
 * own.
 *
 * Transcribing sixteen boolean expressions by hand is exactly the kind of work
 * where one inverted literal is invisible and fatal, so the transcription was
 * checked against an independent implementation of the same device before any
 * of it was believed - see src/dongletest.c, which keeps what that check
 * established.
 */

#include "dongle.h"

#include <stdio.h>
#include <string.h>

/*
 * The sixteen registers, named for the pins they drive so that each line below
 * can be read against the equations it came from. Nothing else in tosemu is
 * written this way; this is not a structure anybody designed, it is a
 * photograph of one, and it is worth more as a photograph than as anything
 * tidier.
 *
 * d8 is pin 22. It is named for what it does rather than where it is because
 * it is the only one of the sixteen that leaves the chip.
 */
struct pins
{
    uint8_t p03, p04, p05, p06, p07, p08, p09, p10;
    uint8_t p15, p16, p17, p18, p19, p20, p21;
    uint8_t d8;
};

static struct pins pins;

/* Which key is plugged in, if any. One is all there is so far, so this is a
 * flag with a name attached rather than a table */
static int wanted;

void dongle_reset(void)
{
    memset(&pins, 0, sizeof pins);
}

/*
 * One clock of the state machine, which is one access to the cartridge port.
 *
 * Every register is computed from the values the others held before this call
 * and only then are any of them stored, because on the chip all sixteen are
 * clocked by the same edge and each sees what the others held rather than what
 * they are about to hold. Computing in place would make every line after the
 * first read a register that had already moved on, and the stream would be
 * wrong from the second clock onwards.
 */
void dongle_clock(int a8)
{
    struct pins n;

    /* Read out once, so that the expressions below look like the equations
     * rather than like a structure being walked */
    const int p03 = pins.p03, p04 = pins.p04, p05 = pins.p05, p06 = pins.p06;
    const int p07 = pins.p07, p08 = pins.p08, p09 = pins.p09, p10 = pins.p10;
    const int p15 = pins.p15, p16 = pins.p16, p17 = pins.p17, p18 = pins.p18;
    const int p19 = pins.p19, p20 = pins.p20, p21 = pins.p21, d8 = pins.d8;

    a8 = a8 ? 1 : 0;

    n.p03 = p03 ^ ((!p03 && !a8)
                   || (p03 && a8));

    n.p04 = p04 ^ ((!p04 && a8)
                   || (p03 && !p04 && !a8)
                   || (p03 && p04 && a8));

    n.p05 = p05 ^ ((p03 && !p05 && a8)
                   || (p04 && p05 && a8)
                   || (p03 && p04 && !p05 && !a8));

    n.p06 = p06 ^ ((p03 && !p06 && !a8)
                   || (p04 && !p05 && p06)
                   || (p03 && p04 && p05 && !p06 && !a8));

    n.p07 = p07 ^ ((!p03 && p05 && !p07)
                   || (!p04 && !p06 && p07 && a8)
                   || (p03 && p04 && p05 && p06 && !p07 && !a8));

    n.p08 = p08 ^ ((!p03 && !p05 && p07 && !p08)
                   || (!p04 && p06 && p08 && a8)
                   || (p03 && p04 && p05 && p06 && p07 && !p08 && !a8));

    n.p09 = p09 ^ ((!p07 && p08 && !p09)
                   || (p04 && !p05 && !p06 && p09)
                   || (p03 && p04 && p05 && p06 && p07 && p08 && !p09 && !a8));

    n.p10 = p10 ^ ((!p04 && p07 && !p08 && !p10)
                   || (p05 && p06 && !p09 && p10)
                   || (p03 && p04 && p05 && p06 && p07 && p08 && p09 && !p10
                       && !a8));

    n.p15 = p15 ^ ((!p07 && p08 && !p15)
                   || (!p06 && p09 && !p10 && p15)
                   || (p03 && p04 && p05 && p06 && p07 && p08 && p09 && p10
                       && !a8 && !p15));

    n.p16 = p16 ^ ((!p09 && !p15 && p16)
                   || (!p08 && p10 && !p16)
                   || (p03 && p04 && p05 && p06 && p07 && p08 && p09 && p10
                       && !a8 && p15 && !p16));

    n.p17 = p17 ^ ((!p08 && p17)
                   || (!p10 && !p16 && !p17)
                   || (p03 && p04 && p05 && p06 && p07 && p08 && p09 && p10
                       && !a8 && p15 && p16 && !p17));

    n.p18 = p18 ^ ((!p15 && p16 && p18)
                   || (p08 && !p10 && p17 && !p18)
                   || (p03 && p04 && p05 && p06 && p07 && p08 && p09 && p10
                       && !a8 && p15 && p16 && p17 && !p18));

    n.p19 = p19 ^ ((p10 && !p15 && !p19)
                   || (p16 && !p17 && p18 && p19)
                   || (p03 && p04 && p05 && p06 && p07 && p08 && p09 && p10
                       && !a8 && p15 && p16 && p17 && p18 && !p19));

    n.p20 = p20 ^ ((!p16 && !p19 && !p20)
                   || (p17 && !p18 && p20)
                   || (p03 && p04 && p05 && p06 && p07 && p08 && p09 && p10
                       && !a8 && p15 && p16 && p17 && p18 && p19 && !p20));

    n.p21 = p21 ^ ((!p17 && p18 && !p21)
                   || (!p16 && p19 && !p20 && p21)
                   || (p03 && p04 && p05 && p06 && p07 && p08 && p09 && p10
                       && !a8 && p15 && p16 && p17 && p18 && p19 && p20
                       && !p21));

    n.d8 = d8 ^ ((!p04 && d8)
                 || (p05 && a8 && !d8)
                 || (p09 && !a8 && !p16 && !p18 && d8)
                 || (!p06 && p09 && a8 && p17 && !p21 && d8)
                 || (p03 && p04 && p05 && p06 && p07 && p08 && p09 && p10
                     && !a8 && p15 && p16 && p17 && p18 && p19 && p20 && p21
                     && !d8));

    pins = n;
}

int dongle_answer(void)
{
    return pins.d8;
}

/*
 * What the port reads as.
 *
 * Only data bit 8 carries anything: everything else on the bus is pulled high,
 * so a word read comes back 0xfeff or 0xffff and the program is looking at the
 * one bit that moved. Bit 8 is the low bit of the high byte, which is the byte
 * at the even address.
 *
 * The register is read before the edge that moves it, which is what being a
 * registered output means - the program sees what the chip was holding when it
 * asked, and the answer to this question appears on the next read. Getting
 * that the other way round yields a stream shifted by one, which matches on a
 * constant input and diverges on everything else; it is the first thing that
 * would be got wrong here and the tests pin it.
 */
static uint8_t byte_of_a_read(uint32_t address)
{
    /*
     * A read of the port is one access however wide it is, so it clocks the
     * chip once - but tosemu turns every access into a byte at a time, so a
     * word read arrives here twice and a long read four times. Clocking on the
     * even half only puts that back: a byte read at an even address is one
     * clock, a word is one, and a long is two, all of which is what the bus
     * would have done.
     *
     * The odd half is the low byte of the same access. It carries nothing and
     * must not clock, or every word the program reads would move the chip on
     * twice and the stream would be the wrong one.
     */
    if (address & 1)
        return 0xff;

    {
        uint8_t answer = (uint8_t)(0xfe | dongle_answer());

        /*
         * And the question is address bit 8, which is the line the chip's
         * input pin is soldered to. Taken from the machine's address rather
         * than from an offset into the area: the two agree while the port
         * starts where it does, and only one of them goes on being the right
         * answer if it ever moves.
         */
        dongle_clock((address >> 8) & 1);

        return answer;
    }
}

uint8_t dongle_area_read(struct _memarea *area, uint32_t address)
{
    (void)area;

    return byte_of_a_read(address);
}

/*
 * Writing to the port does nothing, and quietly.
 *
 * There is no memory out there to write to - what is on the far end is a
 * handful of logic and, on a real cartridge, a ROM - so a write goes nowhere
 * on the machine too. Refusing it would be the usual thing for an area that
 * cannot be written, but refusing halts the emulator, and a program that pokes
 * the cartridge port deserves to be ignored rather than killed.
 */
void dongle_area_write(struct _memarea *area, uint32_t address, uint8_t value)
{
    (void)area;
    (void)address;
    (void)value;
}

/*
 * Which keys there are, which is one.
 *
 * "cubase" is the red key, the one Cubase 3.0, 3.01 and 3.10 and Cubase Score
 * 2.x were shipped with. The black key that came with Cubase 2.x is a
 * different and much smaller machine and is not modelled; a person who asks
 * for it should be told that rather than handed this one, which would answer
 * confidently and wrongly.
 */
int dongle_asked_for(const char *name)
{
    if (name == 0 || *name == '\0')
        return 0;

    if (strcmp(name, "cubase") == 0 || strcmp(name, "cubase3") == 0)
    {
        wanted = 1;
        dongle_reset();

        return 1;
    }

    printf("tosemu: there is no key called %s. The one there is is cubase, "
           "which is the red key Cubase 3 came with.\n", name);

    return 0;
}

int dongle_wanted(void)
{
    return wanted;
}

const char *dongle_named(void)
{
    return wanted ? "cubase" : 0;
}
