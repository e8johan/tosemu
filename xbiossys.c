/*
 * TOSEMU - an emulated environment for TOS applications
 * Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
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
 * System functions: the clock, the random number generator, the battery backed
 * memory, and running a subroutine in supervisor mode.
 *
 * These are the XBIOS calls with a real equivalent on the host, so most of
 * them do the obvious thing. Settime is the exception: an application should
 * not be able to change the clock of the machine tosemu runs on, so it is
 * accepted and ignored.
 */

#include "xbios.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tossystem.h"
#include "memory.h"
#include "cpu.h"
#include "m68k.h"

#include "xbios_p.h"

/* Supervisor mode ***********************************************************/

static uint32_t dreg[5], areg[4];

static void save_regs(void)
{
    int i;
    for(i=0; i<5; i++)
        dreg[i] = m68k_get_reg(0, M68K_REG_D3+i);
    for(i=0; i<4; i++)
        areg[i] = m68k_get_reg(0, M68K_REG_A3+i);
}

static void restore_regs(void)
{
    int i;
    for(i=0; i<5; i++)
        m68k_set_reg(M68K_REG_D3+i, dreg[i]);
    for(i=0; i<4; i++)
        m68k_set_reg(M68K_REG_A3+i, areg[i]);
}

/* Supexec has been implemented using magic memory, which provides a mechanism
 * for triggering a callback. The general idea is:
 *
 * 1. Switch to supervisor mode, thus switching stack
 * 2. Push the current PC, i.e. the return address
 * 3. Push the magic value 0x200
 * 4. Set the PC to the sub-routine address
 *
 * When the RTS call is made, the PC will be set to 0x200, triggering a read to
 * the addresses 0x200 and 0x201. This will hit the magic memory area
 * registered for this purpose, resulting in calls to magic_xbios_supexec_read.
 * When 0x201 is called, the PC is popped from the stack before the supervisor
 * mode is disabled, PC updated and D0 set to 0 (the XBIOS return code for no
 * error).  The instruction at 0x200 will be executed before the PC update
 * takes effect, so the memory reads return a NOP at that address.
 */
uint32_t XBIOS_Supexec()
{
    uint32_t lv0 = peek_u32(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", lv0);
    }

    save_regs();

    enable_supervisor_mode();
    push_u32(m68k_get_reg(0, M68K_REG_PC));
    push_u32(0x200);
    m68k_set_reg(M68K_REG_PC, lv0);

    return 0;
}

/* Magic memory for supexec */
uint8_t magic_xbios_supexec_read(struct _memarea *area, uint32_t address)
{
    uint32_t lv0;
    if (address == 0x200)
        return 0x4e;
    else if (address == 0x201)
    {
        lv0 = pop_u32();
        disable_supervisor_mode();
        m68k_set_reg(M68K_REG_PC, lv0);
        restore_regs();
        return 0x71;
    }

    return 0;
}

/* Protection for magic memory for supexec */
void magic_xbios_supexec_write(struct _memarea *area, uint32_t address, uint8_t value)
{
    printf("Attempted to write to magic memory at 0x%x\n", address);
    halt_execution();
}

/* Clock functions ***********************************************************/

uint32_t XBIOS_Gettime()
{
    /*
     * A packed date in the high word and a packed time in the low word:
     *
     * 0-4     Seconds in units of two (0-29)
     * 5-10    Minutes (0-59)
     * 11-15   Hours (0-23)
     * 16-20   Day (1-31)
     * 21-24   Month (1-12)
     * 25-31   Year (0-119, 0 = 1980)
     *
     * http://toshyp.atari.org/en/00400b.html
     */
    time_t t;
    struct tm *lt;

    FUNC_TRACE_ENTER

    t = time(NULL);
    lt = localtime(&t);

    return (lt->tm_sec / 2) |
           (lt->tm_min << 5) |
           (lt->tm_hour << 11) |
           (lt->tm_mday << 16) |
           ((lt->tm_mon+1) << 21) |
           ((lt->tm_year-80) << 25);
}

uint32_t XBIOS_Settime()
{
    uint32_t datetime = peek_u32(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    datetime: 0x%x\n", datetime);
    }

    /* The clock belongs to the machine tosemu runs on, not to the application,
     * so accept this and leave it alone. Gettime keeps reporting the host
     * clock, which is what an application asking the time wants anyway. */

    return XBIOS_E_OK;
}

/* Random numbers ************************************************************/

uint32_t XBIOS_Random()
{
    FUNC_TRACE_ENTER

    /* A 24 bit value, http://toshyp.atari.org/en/004009.html */
    return random() & 0xffffff;
}

/* Battery backed memory *****************************************************/

#define NVM_SIZE (50)

/* The clock chip on an STE and later holds 50 bytes that survive a power off.
 * There is nothing to survive here, but an application that writes a setting
 * and reads it back within one run should see what it wrote. */
static uint8_t nvm[NVM_SIZE];

uint32_t XBIOS_NVMaccess()
{
    uint16_t op = peek_u16(2);
    uint16_t start = peek_u16(4);
    uint16_t count = peek_u16(6);
    uint32_t buffer = peek_u32(8);
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    op: %d, start: %d, count: %d, buffer: 0x%x\n",
               op, start, count, buffer);
    }

    if (op != 2 && (start >= NVM_SIZE || count > NVM_SIZE - start))
        return XBIOS_ERROR;

    switch (op)
    {
    case 0: /* Read */
        for (i = 0; i < count; ++i)
            m68k_write_memory_8(buffer + i, nvm[start + i]);
        break;
    case 1: /* Write */
        for (i = 0; i < count; ++i)
            nvm[start + i] = m68k_read_memory_8(buffer + i);
        break;
    case 2: /* Clear the whole thing */
        memset(nvm, 0, sizeof nvm);
        break;
    default:
        return XBIOS_ERROR;
    }

    return XBIOS_E_OK;
}

/* Debugging *****************************************************************/

uint32_t XBIOS_Dbmsg()
{
    uint16_t rsrvd = peek_u16(2);
    uint16_t msg_num = peek_u16(4);
    uint32_t ptr = peek_u32(6);
    uint8_t ch;

    FUNC_TRACE_ENTER_ARGS {
        printf("    rsrvd: 0x%x, msg_num: 0x%x, ptr: 0x%x\n", rsrvd, msg_num, ptr);
    }

    /* An application uses this to say something to whoever is debugging it,
     * and tosemu is the closest thing to a debugger it has. Message 0xF100
     * means ptr is a string, http://toshyp.atari.org/en/004008.html */
    if (rsrvd == 0x5abc && msg_num == 0xf100 && ptr)
    {
        printf("Dbmsg: ");
        while ((ch = m68k_read_disassembler_8(ptr++)))
            putchar(ch);
        putchar('\n');
    }

    return XBIOS_E_OK;
}

/* MetaDOS *******************************************************************/

uint32_t XBIOS_Metainit()
{
    uint32_t buffer = peek_u32(2);
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    buffer: 0x%x\n", buffer);
    }

    /* There is no MetaDOS here. Zeroing the structure is how that is reported:
     * no drive map and no version string, rather than leaving the caller
     * reading whatever its buffer happened to hold. */
    for (i = 0; i < 12; ++i)
        m68k_write_memory_8(buffer + i, 0);

    return XBIOS_E_OK;
}
