/*
 * TOSEMU - an emulated environment for TOS applications
 * Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
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

#include "memory.h"

#include <stdio.h>
#include <stdlib.h>

#include "tossystem.h"
#include "cpu.h"
#include "m68k.h"

/* Memory area linked list head */
static struct _memarea *head = 0;

/*
 * The area the last lookup found.
 *
 * Every byte the machine reads or writes comes through here, instruction
 * fetches included, and a fetch is two of them. Walking the list for each one
 * made what an access costs depend on how many devices are mapped - and the
 * list is searched newest first, so the areas a program actually lives in are
 * at the far end of it: the application's RAM was seven deep and anything
 * registered later went in front of it. Running Cubase as far as it gets,
 * that came to a hundred and forty eight million steps for thirty million
 * lookups, very nearly five every time.
 *
 * One remembered area answers essentially all of them. A program reads the
 * instruction it is about to run and the data beside it, so consecutive
 * accesses are in the same area almost always - the same measurement with this
 * in place is one step per lookup, a miss every two hundred accesses. What it
 * really buys is that the cost stops depending on how many devices there are,
 * which is what makes a device that is always mapped free when nothing is
 * using it.
 *
 * It has to be given up whenever an area is, which is what remove_memory_area
 * does: the areas are freed when a machine is torn down and a remembered
 * pointer into one would outlive it.
 */
static struct _memarea *last_found;

/* Support functions */
static uint8_t ptr_read(struct _memarea *area, uint32_t address)
{
    return ((uint8_t *)area->ptr)[address - area->base];
}

static void ptr_write(struct _memarea *area, uint32_t address, uint8_t value)
{
    ((uint8_t *)area->ptr)[address - area->base] = value;
}

int add_ptr_memory_area(char *name, uint8_t flags, uint32_t base, uint32_t len, void *ptr)
{
    return add_fnct_memory_area(name, flags, base, len, ptr, ptr_read, ptr_write);
}

int add_fnct_memory_area(char *name, uint8_t flags, uint32_t base, uint32_t len, void *ptr, uint8_t (*read)(struct _memarea*, uint32_t), void (*write)(struct _memarea*, uint32_t, uint8_t))
{
    struct _memarea *area;
    
    /* TODO ensure that we do not have memory area collisions */
    
    area = malloc(sizeof(struct _memarea));
    if (!area) {
        printf("Failed to allocate memory area for 0x%x\n", base);
        return 1;
    }
    
    area->base = base;
    area->len = len;
    area->read = read;
    area->write = write;
    area->ptr = ptr;
    area->flags = flags;
    area->next = head;
    head = area;
        
    return 0;
}

int remove_memory_area(uint32_t base)
{
    struct _memarea *ptr = head;
    struct _memarea *prev = 0;

    /* Whatever was remembered may be the one going away, and there is no
     * reason to work out whether it is: the next lookup pays one walk */
    last_found = 0;

    while (ptr)
    {
        if (ptr->base == base) {
            if (prev)
                prev->next = ptr->next;
            else
                head = ptr->next;
            
            free(ptr);
            return 0;
        }
        
        prev = ptr;
    }
    
    printf("Failed to remove memory area at 0x%x\n", base);
    return 1;
}

void reset_memory()
{
    while (head)
        remove_memory_area(head->base);
}


struct _memarea *find_memarea(uint32_t address)
{
    struct _memarea *area = last_found;

    if (area && address >= area->base && address < area->base + area->len)
        return area;

    area = head;

    while(area)
    {
        if (address >= area->base && address < area->base + area->len)
        {
            last_found = area;
            break;
        }

        area = area->next;
    }

    return area;
}

void *tos_mem_to_host_mem(uint32_t address)
{
    struct _memarea *area = find_memarea(address);
    
    if (!area) {
        halt_execution();
        printf("Attempted to get direct access to non-existing memory at 0x%x\n", address);
        return 0;
    }
    
    if (area->write != ptr_write || area->read != ptr_read)
    {
        halt_execution();
        printf("Attempted to get direct access to non-mapped memory at 0x%x\n", address);
        return 0;
    }
    
    return &(((uint8_t *)area->ptr)[address - area->base]);
}


/* These are the real read/write functions */

uint8_t tos_read(uint32_t address)
{
    struct _memarea *area = find_memarea(address);
    uint8_t mask;
    
    if (!area) {
        halt_execution();
        printf("Attempted to read non-existing memory at 0x%x\n", address);
        return 0;
    }
    
    /* Is the CPU in supervisor mode? */
    if (is_supervisor_mode_enabled())
        mask = MEMORY_READ | MEMORY_SUPERREAD;
    else
        mask = MEMORY_READ;
    
    if ((area->flags & mask) != 0)
        return area->read(area, address);
    else {
        halt_execution();
        printf("Attempted to read non-readable memory at 0x%x\n", address);
        return 0;
    }
}

void tos_write(uint32_t address, uint8_t value)
{
    struct _memarea *area = find_memarea(address);
    uint8_t mask;
    
    if (!area) {
        halt_execution();
        printf("Attempted to write to non-existing memory at 0x%x\n", address);
        return;
    }
    
    /* Is the CPU in supervisor mode? */
    if (is_supervisor_mode_enabled())
        mask = MEMORY_WRITE | MEMORY_SUPERWRITE;
    else
        mask = MEMORY_WRITE;
    
    if ((area->flags & mask) != 0)
        area->write(area, address, value);
    else {
        halt_execution();
        printf("Attempted to write to non-writeable memory at 0x%x\n", address);
    }
}

/*
 * The several bytes of one access, looked up once.
 *
 * A word is two bytes and a long is four, and each of them used to go the
 * whole way round on its own: find the area, ask the CPU what mode it is in,
 * check the flags, then read. All of that is the same answer every time for an
 * access that lies inside one area, which is every access a program makes
 * except the ones that are already going wrong.
 *
 * What is not hoisted is the read itself. Each byte still goes through the
 * area's own function, because for some areas reading is not a lookup but an
 * event - the cartridge port clocks a key on the byte at the even address, and
 * the magic memory a midivec points at does its work on the way past. Reading
 * four bytes in one go would be quicker and would stop those being what they
 * are.
 *
 * Anything that is not a plain access inside one area falls back to going a
 * byte at a time through tos_read, so that an access running off the end of an
 * area, or to an address with nothing at it, or from a mode that may not,
 * stops exactly where it did before and says exactly what it did before.
 */
static int one_area_holds(struct _memarea *area, uint32_t address, int n)
{
    return area && (address - area->base) + (uint32_t)n <= area->len;
}

static unsigned int read_run(uint32_t address, int n)
{
    struct _memarea *area = find_memarea(address);
    unsigned int res = 0;
    int i;

    if (one_area_holds(area, address, n))
    {
        uint8_t mask = is_supervisor_mode_enabled()
                     ? (uint8_t)(MEMORY_READ | MEMORY_SUPERREAD)
                     : (uint8_t)MEMORY_READ;

        if ((area->flags & mask) != 0)
        {
            for (i = 0; i < n; i++)
                res = (res << 8) | area->read(area, address + i);

            return res;
        }
    }

    for (i = 0; i < n; i++)
        res = (res << 8) | tos_read(address + i);

    return res;
}

static void write_run(uint32_t address, unsigned int value, int n)
{
    struct _memarea *area = find_memarea(address);
    int i;

    if (one_area_holds(area, address, n))
    {
        uint8_t mask = is_supervisor_mode_enabled()
                     ? (uint8_t)(MEMORY_WRITE | MEMORY_SUPERWRITE)
                     : (uint8_t)MEMORY_WRITE;

        if ((area->flags & mask) != 0)
        {
            for (i = n - 1; i >= 0; i--)
            {
                area->write(area, address + i, (uint8_t)(value & 0xff));
                value >>= 8;
            }

            return;
        }
    }

    for (i = n - 1; i >= 0; i--)
    {
        tos_write(address + i, (uint8_t)(value & 0xff));
        value >>= 8;
    }
}

/* These are the read/write functions used by Musashi */

unsigned int  m68k_read_disassembler_8(unsigned int address)
{
    return tos_read(address);
}
unsigned int  m68k_read_disassembler_16(unsigned int address)
{
    return read_run(address, 2);
}
unsigned int  m68k_read_disassembler_32(unsigned int address)
{
    return read_run(address, 4);
}

unsigned int  m68k_read_memory_8(unsigned int address)
{
    return m68k_read_disassembler_8(address);
}
unsigned int  m68k_read_memory_16(unsigned int address)
{
    return m68k_read_disassembler_16(address);
}
unsigned int  m68k_read_memory_32(unsigned int address)
{
    return m68k_read_disassembler_32(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    tos_write(address, value);
}
void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    write_run(address, value, 2);
}
void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    write_run(address, value, 4);
}
