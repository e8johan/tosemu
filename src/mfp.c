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

/* See mfp.h for what this is and for the two rules that are not obvious. */

#include "mfp.h"

/* Where each register sits, as an offset from the base. Odd, every one of
 * them, the chip being on the low half of a sixteen bit bus. */
#define MFP_GPIP  (0x01)
#define MFP_AER   (0x03)
#define MFP_DDR   (0x05)
#define MFP_IERA  (0x07)
#define MFP_IERB  (0x09)
#define MFP_IPRA  (0x0B)
#define MFP_IPRB  (0x0D)
#define MFP_ISRA  (0x0F)
#define MFP_ISRB  (0x11)
#define MFP_IMRA  (0x13)
#define MFP_IMRB  (0x15)
#define MFP_VR    (0x17)
#define MFP_TACR  (0x19)
#define MFP_TBCR  (0x1B)
#define MFP_TCDCR (0x1D)
#define MFP_TADR  (0x1F)
#define MFP_TBDR  (0x21)
#define MFP_TCDR  (0x23)
#define MFP_TDDR  (0x25)
#define MFP_SCR   (0x27)
#define MFP_UCR   (0x29)
#define MFP_RSR   (0x2B)
#define MFP_TSR   (0x2D)
#define MFP_UDR   (0x2F)

/* Bit 3 of the vector register asks for the in-service bits to be kept, so
 * that a handler has to clear its own. Without it they are never set and a
 * channel does not hold off the ones below it. */
#define VR_SOFTWARE_EOI (0x08)

/*
 * The clock the timers count, in hertz. Everything about how long a timer runs
 * for comes from this number and it is worth one check rather than a comment:
 * EmuTOS asks for the system clock with a divider of 64 and a count of 192,
 * and 2457600 / 64 / 192 is 200 exactly.
 */
#define MFP_TIMER_CLOCK (2457600L)

static struct {
    uint8_t gpip, aer, ddr;
    uint8_t iera, ierb;
    uint8_t ipra, iprb;
    uint8_t isra, isrb;
    uint8_t imra, imrb;
    uint8_t vr;
    uint8_t tacr, tbcr, tcdcr;
    uint8_t tadr, tbdr, tcdr, tddr;
    uint8_t scr, ucr, rsr, tsr, udr;
} mfp;

/*
 * Which half of the sixteen a channel is in.
 *
 * This is the one that is easy to read backwards. The A registers hold the
 * upper eight channels and the B registers the lower eight, so Timer A - being
 * channel 13 - is in IERA, but Timer B - channel 8 - is in IERA as well, and
 * Timer C at channel 5 is in IERB. The letter on the register has nothing to
 * do with the letter on the timer.
 */
static int in_a(int channel)
{
    return channel >= 8;
}

static uint8_t bit_of(int channel)
{
    return (uint8_t)(1 << (in_a(channel) ? channel - 8 : channel));
}

static uint8_t *reg_for(int channel, uint8_t *a, uint8_t *b)
{
    return in_a(channel) ? a : b;
}

void mfp_reset(void)
{
    int i;
    uint8_t *bytes = (uint8_t *)&mfp;

    for (i = 0; i < (int)sizeof mfp; i++)
        bytes[i] = 0;

    /*
     * Every general purpose input reads high, which for the ones that are
     * active low - the ACIAs among them - is nothing happening. A chip that
     * came up with them all low would look like every device in the machine
     * asking for attention at once.
     */
    mfp.gpip = 0xff;

    /* Vectors 0x40 to 0x4F, and the in-service bits kept, which is what TOS
     * sets and therefore what a program that never writes this one expects */
    mfp.vr = 0x48;
}

/* Channels *****************************************************************/

void mfp_raise(int channel)
{
    uint8_t bit;

    if (channel < 0 || channel > 15)
        return;

    bit = bit_of(channel);

    /*
     * A channel that is not enabled is not listening, and nothing is
     * remembered about what it missed. That is the chip's behaviour and it is
     * what makes Jdisint mean anything: a disabled channel that quietly
     * collected pending bits would fire the moment it was enabled again.
     */
    if (!(*reg_for(channel, &mfp.iera, &mfp.ierb) & bit))
        return;

    *reg_for(channel, &mfp.ipra, &mfp.iprb) |= bit;
}

void mfp_gpip(int bit, int high)
{
    /* Which channel each of the eight inputs interrupts on. The four low ones
     * are the four low channels; the rest are scattered, which is how the chip
     * is wired rather than anything with a pattern in it. */
    static const int channel_of[8] = { 0, 1, 2, 3, 6, 7, 14, 15 };
    uint8_t mask;
    int was_high;

    if (bit < 0 || bit > 7)
        return;

    mask = (uint8_t)(1 << bit);
    was_high = (mfp.gpip & mask) != 0;

    if (high)
        mfp.gpip |= mask;
    else
        mfp.gpip &= (uint8_t)~mask;

    if (was_high == (high != 0))
        return;

    /*
     * Only the edge the active edge register asks for. A one there means the
     * channel wants a nought to one transition and a nought means the other
     * way about, which is how a line that means something by being low - an
     * interrupt line, as both the ACIAs have - is watched for going low.
     */
    if (((mfp.aer & mask) != 0) == (high != 0))
        mfp_raise(channel_of[bit]);
}

/*
 * Whether anything at or above this channel is being serviced.
 *
 * A channel holds off everything below it while its handler runs, which is
 * what stops a slow handler being interrupted by something less urgent and
 * then never finishing. Equal counts as above: a channel does not interrupt
 * itself.
 */
static int blocked_by_service(int channel)
{
    int c;

    for (c = 15; c >= channel; c--)
        if (*reg_for(c, &mfp.isra, &mfp.isrb) & bit_of(c))
            return 1;

    return 0;
}

int mfp_pending_channel(void)
{
    int c;

    /* Highest first, that being what priority means here */
    for (c = 15; c >= 0; c--)
    {
        uint8_t bit = bit_of(c);

        if (!(*reg_for(c, &mfp.ipra, &mfp.iprb) & bit))
            continue;

        /* Masked off is pending but not asking. The difference matters: the
         * pending bit stays set, so a channel unmasked later fires at once
         * rather than having lost what happened. */
        if (!(*reg_for(c, &mfp.imra, &mfp.imrb) & bit))
            continue;

        if (blocked_by_service(c))
            continue;

        return c;
    }

    return -1;
}

int mfp_vector_of(int channel)
{
    if (channel < 0 || channel > 15)
        return -1;

    return (mfp.vr & 0xf0) | channel;
}

int mfp_acknowledge(void)
{
    int channel = mfp_pending_channel();
    uint8_t bit;

    if (channel < 0)
        return -1;

    bit = bit_of(channel);

    *reg_for(channel, &mfp.ipra, &mfp.iprb) &= (uint8_t)~bit;

    /*
     * In service, but only if the chip was asked to keep track. Without the
     * software end of interrupt bit the chip clears the in-service bit itself
     * as the interrupt is taken, which means nothing is held off and a handler
     * has nothing to clear - so setting the bit then would wedge every lower
     * channel until something wrote it away.
     */
    if (mfp.vr & VR_SOFTWARE_EOI)
        *reg_for(channel, &mfp.isra, &mfp.isrb) |= bit;

    return mfp_vector_of(channel);
}

void mfp_finished(int channel)
{
    if (channel < 0 || channel > 15)
        return;

    *reg_for(channel, &mfp.isra, &mfp.isrb) &= (uint8_t)~bit_of(channel);
}

void mfp_enable(int channel)
{
    uint8_t bit;

    if (channel < 0 || channel > 15)
        return;

    bit = bit_of(channel);

    *reg_for(channel, &mfp.iera, &mfp.ierb) |= bit;
    *reg_for(channel, &mfp.imra, &mfp.imrb) |= bit;
}

void mfp_disable(int channel)
{
    uint8_t bit;

    if (channel < 0 || channel > 15)
        return;

    bit = (uint8_t)~bit_of(channel);

    /* All four, which is what EmuTOS's disable_mfp_interrupt does. A channel
     * left pending or in service after being turned off is one that fires the
     * moment it comes back, or one that holds off everything below it for ever */
    *reg_for(channel, &mfp.imra, &mfp.imrb) &= bit;
    *reg_for(channel, &mfp.iera, &mfp.ierb) &= bit;
    *reg_for(channel, &mfp.ipra, &mfp.iprb) &= bit;
    *reg_for(channel, &mfp.isra, &mfp.isrb) &= bit;
}

/* Timers *******************************************************************/

int mfp_timer_channel(int timer)
{
    static const int channel_of[MFP_TIMER_COUNT] = {
        MFP_TIMER_A, MFP_TIMER_B, MFP_TIMER_C, MFP_TIMER_D
    };

    if (timer < 0 || timer >= MFP_TIMER_COUNT)
        return -1;

    return channel_of[timer];
}

long mfp_period_from(uint8_t control, uint8_t data)
{
    /* What each setting of the low three bits divides the clock by. The first
     * is not a divider: nought means the timer is not running at all. */
    static const long divider[8] = { 0, 4, 10, 16, 50, 64, 100, 200 };
    long count;

    control &= 0x0f;

    if (control == 0)
        return 0;

    /*
     * Eight and above are the modes that count something rather than waiting
     * for a length of time - edges arriving on a pin, and how long a pulse
     * lasts. Neither is a number of nanoseconds, so they are refused rather
     * than quietly timed as though they were delays.
     */
    if (control & 0x08)
        return -1;

    /* A data register of nought counts the whole way round, which is two
     * hundred and fifty six and not none */
    count = data ? (long)data : 256L;

    /* In a wider type and rounded, because the clock does not divide evenly
     * into a second and the error otherwise accumulates: a sequencer's tempo
     * drifts by a bar an hour on a rounding that always goes the same way. */
    return (long)(((long long)1000000000L * divider[control] * count
                   + MFP_TIMER_CLOCK / 2) / MFP_TIMER_CLOCK);
}

long mfp_timer_period(int timer)
{
    switch (timer)
    {
    case 0:
        return mfp_period_from(mfp.tacr & 0x0f, mfp.tadr);
    case 1:
        return mfp_period_from(mfp.tbcr & 0x0f, mfp.tbdr);

    /*
     * Timers C and D share a register, C in the upper half and D in the lower.
     * They have three bits each rather than four, having only the delay mode
     * between them - which is why neither can answer with a refusal above.
     */
    case 2:
        return mfp_period_from((mfp.tcdcr >> 4) & 0x07, mfp.tcdr);
    case 3:
        return mfp_period_from(mfp.tcdcr & 0x07, mfp.tddr);
    }

    return 0;
}

void mfp_setup_timer(int timer, uint8_t control, uint8_t data)
{
    /*
     * Written in the order EmuTOS's setup_timer writes them: the control
     * register is cleared, then the data register is set, then the control
     * register is set to what was asked for. Loading the count while the timer
     * is stopped is what makes it take effect at once rather than at the end
     * of however long the previous setting had left to run.
     */
    switch (timer)
    {
    case 0:
        mfp.tacr = 0;
        mfp.tadr = data;
        mfp.tacr = control;
        break;
    case 1:
        mfp.tbcr = 0;
        mfp.tbdr = data;
        mfp.tbcr = control;
        break;

    /* And for the two that share a register, only that half of it is
     * disturbed - the other timer is very likely running */
    case 2:
        mfp.tcdcr &= 0x0f;
        mfp.tcdr = data;
        mfp.tcdcr |= control & 0xf0;
        break;
    case 3:
        mfp.tcdcr &= 0xf0;
        mfp.tddr = data;
        mfp.tcdcr |= control & 0x0f;
        break;
    }
}

/* The registers as memory ***************************************************/

static uint8_t *register_at(uint32_t offset)
{
    switch (offset)
    {
    case MFP_GPIP:  return &mfp.gpip;
    case MFP_AER:   return &mfp.aer;
    case MFP_DDR:   return &mfp.ddr;
    case MFP_IERA:  return &mfp.iera;
    case MFP_IERB:  return &mfp.ierb;
    case MFP_IPRA:  return &mfp.ipra;
    case MFP_IPRB:  return &mfp.iprb;
    case MFP_ISRA:  return &mfp.isra;
    case MFP_ISRB:  return &mfp.isrb;
    case MFP_IMRA:  return &mfp.imra;
    case MFP_IMRB:  return &mfp.imrb;
    case MFP_VR:    return &mfp.vr;
    case MFP_TACR:  return &mfp.tacr;
    case MFP_TBCR:  return &mfp.tbcr;
    case MFP_TCDCR: return &mfp.tcdcr;
    case MFP_TADR:  return &mfp.tadr;
    case MFP_TBDR:  return &mfp.tbdr;
    case MFP_TCDR:  return &mfp.tcdr;
    case MFP_TDDR:  return &mfp.tddr;
    case MFP_SCR:   return &mfp.scr;
    case MFP_UCR:   return &mfp.ucr;
    case MFP_RSR:   return &mfp.rsr;
    case MFP_TSR:   return &mfp.tsr;
    case MFP_UDR:   return &mfp.udr;
    }

    return 0;
}

uint8_t mfp_register(uint32_t offset)
{
    uint8_t *reg = register_at(offset);

    return reg ? *reg : 0;
}

uint8_t mfp_read_at(uint32_t offset)
{
    return mfp_register(offset);
}

void mfp_write_at(uint32_t offset, uint8_t value)
{
    uint8_t *reg = register_at(offset);

    if (!reg)
        return;

    switch (offset)
    {
    /*
     * The registers that are cleared rather than set.
     *
     * A nought written clears that bit and a one leaves it alone, which is the
     * opposite way round from every other register here and is what lets a
     * handler clear its own channel with one write without losing anything
     * that arrived while it was running. TOS's ACIA handler writes 0xBF to
     * ISRB meaning "clear bit six", not "set the other seven".
     */
    case MFP_IPRA:
    case MFP_IPRB:
    case MFP_ISRA:
    case MFP_ISRB:
        *reg &= value;
        return;

    /*
     * Turning a channel off takes its pending bit with it, the two being
     * written together everywhere TOS turns one off. A channel left pending
     * after being disabled fires the moment it is enabled again, which is a
     * program being interrupted by something it asked to stop hearing about.
     */
    case MFP_IERA:
        mfp.iera = value;
        mfp.ipra &= value;
        return;
    case MFP_IERB:
        mfp.ierb = value;
        mfp.iprb &= value;
        return;

    /* Taking the software end of interrupt away clears what it was keeping,
     * there being nothing left to clear those bits afterwards */
    case MFP_VR:
        mfp.vr = value;
        if (!(value & VR_SOFTWARE_EOI))
        {
            mfp.isra = 0;
            mfp.isrb = 0;
        }
        return;

    /* The general purpose inputs are pins. What the machine writes there goes
     * nowhere; what it reads is what the devices are saying. */
    case MFP_GPIP:
        return;
    }

    *reg = value;
}
