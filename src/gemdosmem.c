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

#include "gemdosmem_p.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cpu.h"

#include "gemdos_p.h"

/* Memory management functions ***********************************************/

/* The memory areas are stored in a sorted order by base */
struct mem_area;
struct mem_area {
    uint32_t base, len;

    /* Whether the program that owns this has gone and left it behind. A
     * resident block is never freed and never handed out again - see mem_keep,
     * and Ptermres, which is the only thing that sets it. */
    int resident;

    struct mem_area *next;
};
struct mem_area *mem_list;
static uint32_t mem_allocatable_top;

/* How much room is left above the last block.
 *
 * The top is an address rather than a size, so a block reaching it leaves
 * nothing rather than the whole address space that subtracting the other way
 * round would appear to give.
 */
static uint32_t space_above(uint32_t prev_top)
{
    if (prev_top >= mem_allocatable_top)
        return 0;

    return mem_allocatable_top - prev_top;
}

static struct mem_area * find_mem_area(uint32_t base, struct mem_area **prevptr)
{
    struct mem_area *ptr = mem_list;
    if (prevptr)
        *prevptr = 0;
    while (ptr)
    {
        if (ptr->base == base)
            break;
        if (prevptr)
            *prevptr = ptr;
        ptr = ptr->next;
    }
    
    return ptr;
}

uint32_t GEMDOS_Mshrink()
{
    struct mem_area *ma;
    
    uint32_t newsiz = peek_u32(8);
    uint32_t block = peek_u32(4);
    
    FUNC_TRACE_ENTER_ARGS {
        printf("    ns: 0x%x, b: 0x%x\n", newsiz, block);
    }
    
    ma = find_mem_area(block, 0);
    if (!ma)
        return GEMDOS_EIMBA;
    if (ma->len < newsiz)
        return GEMDOS_EGSBF;
    
    ma->len = newsiz;

    return 0;
}

uint32_t mem_largest_free(void)
{
    struct mem_area *prev, *ptr;
    uint32_t prev_top, max_free = 0;

    prev = mem_list;
    if (prev)
        ptr = mem_list->next;
    else
        ptr = 0;

    while (ptr)
    {
        prev_top = prev->base + prev->len;
        if (max_free < ptr->base - prev_top)
            max_free = ptr->base - prev_top;

        prev = ptr;
        ptr = ptr->next;
    }

    /* Look at the gap at the end */
    if (prev)
        prev_top = prev->base + prev->len;
    else
        prev_top = 0x900;

    if (max_free < space_above(prev_top))
        max_free = space_above(prev_top);

    return max_free;
}

uint32_t mem_alloc(uint32_t newsiz)
{
    /* This is the tricky mem function, stay safe if changing it.
     * 
     * - Managed memory starts from 0x800 to mem_allocatable_top.
     * - Memory between 0x800 - 0x900 cannot be deallocated [1].
     * - Memory can be allocated from 0x900 to the end of managed memory.
     * - mem_area structures are sorted by base address and never overlap.
     * 
     *  [1] Mfree and Mshrink does not care.
     * 
     * The algorithm works like this:
     * 
     * - Iterate current mem_area structures, look for gaps of the right size.
     * - If no gaps, look for space at the end of allocated memory.
     * - When memory is found:
     *   - Create a new mem_area
     *   - Insert in the correct place in the mem_list linked mem_list
     *   - Return new base
     * - Else:
     *   - Return error
     * 
     * TODO room for improvement, to avoid fragmentation, use the smallest gap
     *      suitable when allocating, right now the first suitable gap is used.
     */
    
    struct mem_area *prev, *ptr, *n;
    uint32_t prev_top;

    {
        prev = mem_list;
        if (prev)
            ptr = mem_list->next;
        else
            ptr = 0;
        
        while (ptr)
        {
            prev_top = prev->base + prev->len;
            if (ptr->base - prev_top >= newsiz)
            {
                /* Large enough gap found */
                
                /* Allocate new area */
                n = malloc(sizeof(struct mem_area));
                memset(n, 0, sizeof(struct mem_area));
                
                /* Set base and len */
                n->base = prev_top;
                n->len = newsiz;
                
                /* Insert into list */
                n->next = ptr;
                prev->next = n;
                
                /* Return new base */
                return n->base;
            }
            
            prev = ptr;
            ptr = ptr->next;
        }
        
        if (prev)
            prev_top = prev->base + prev->len;
        else
            prev_top = 0x900;
        
        if (newsiz <= space_above(prev_top))
        {
            /* Large enough gap found at the end (which can be the start) */
            
            /* Allocate new area */
            n = malloc(sizeof(struct mem_area));
            memset(n, 0, sizeof(struct mem_area));
            
            /* Set base and len */
            n->base = prev_top;
            n->len = newsiz;
            
            /* Insert into list */
            if (prev)
                prev->next = n;
            else
                mem_list = n;
            
            /* Return new base */
            return n->base;
        }
        
        return 0; /* NULL pointer, indicating no new memory allocated */
    }
}

uint32_t GEMDOS_Malloc()
{
    int32_t newsiz = peek_s32(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    newsiz: %d (0x%x)\n", newsiz, newsiz);
    }

    /* An application asks how much it could have by asking for -1 */
    if (newsiz == -1)
        return mem_largest_free();

    return mem_alloc(newsiz);
}

/* Which memory a block is to come from, as Mxalloc's mode word spells it. The
 * bits above these are MiNT's memory protection, which needs an MMU to mean
 * anything, so they are masked off the way EmuTOS masks them. */
#define MX_STRAM     0
#define MX_ALTRAM    1
#define MX_PREFSTRAM 2
#define MX_PREFALT   3
#define MX_MODEMASK  0x03

/* Mxalloc, which is Malloc with a say in where the memory comes from.
 *
 * The emulated machine has one kind, so asking for ST RAM and asking for
 * either kind come to the same thing. A request for alternative RAM and
 * nothing else is the one that has to fail: there is none, and answering it
 * with ST RAM would hand back the very memory the caller said it did not want.
 * That is also what EmuTOS does on a machine built without alternative RAM -
 * the case falls through to the one for a mode it does not know.
 */
uint32_t GEMDOS_Mxalloc()
{
    int32_t amount = peek_s32(2);
    uint16_t mode = peek_u16(6) & MX_MODEMASK;

    FUNC_TRACE_ENTER_ARGS {
        printf("    amount: %d (0x%x), mode: %d\n", amount, amount, mode);
    }

    if (mode == MX_ALTRAM)
        return 0;

    /* An application asks how much it could have by asking for -1 */
    if (amount == -1)
        return mem_largest_free();

    /* A block of nothing is not something to hand out an address for, and a
     * negative size would be a very large one once the allocator has it */
    if (amount <= 0)
        return 0;

    return mem_alloc(amount);
}

int32_t mem_free(uint32_t block)
{
    struct mem_area *ma, *prev;

    ma = find_mem_area(block, &prev);

    if (!ma)
        return GEMDOS_EIMBA;

    /*
     * A program that stayed resident is not there to be freed by whoever comes
     * after it. On an ST the memory simply was not in the free list any more
     * and nothing had its address; here an application walking blocks could
     * reach it, and freeing it would hand the code somebody is still calling
     * to the next thing that asks for memory.
     */
    if (ma->resident)
        return GEMDOS_EIMBA;

    if (prev)
        prev->next = ma->next;
    else
        mem_list = ma->next;

    free(ma);

    return 0;
}

uint32_t GEMDOS_Mfree()
{
    uint32_t block = peek_u32(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", block);
    }

    return mem_free(block);
}


/*
 * A block the caller has already decided the base and length of, put into the
 * list as though it had been allocated.
 *
 * What wants this is loading a program somewhere particular rather than
 * wherever there is room - the program that runs on top of a resident one goes
 * above it, and its block has to be in the list before it Mshrinks or Mallocs
 * anything.
 *
 * Answers 0 when the block would overlap something already there, which is a
 * mistake in the caller rather than the machine running out of memory.
 */
int mem_claim(uint32_t base, uint32_t len)
{
    struct mem_area *prev = 0, *ptr = mem_list, *n;

    while (ptr && ptr->base < base)
    {
        if (ptr->base + ptr->len > base)
            return 0;

        prev = ptr;
        ptr = ptr->next;
    }

    if (ptr && base + len > ptr->base)
        return 0;

    n = malloc(sizeof(struct mem_area));
    if (!n)
        return 0;

    memset(n, 0, sizeof(struct mem_area));
    n->base = base;
    n->len = len;
    n->next = ptr;

    if (prev)
        prev->next = n;
    else
        mem_list = n;

    return 1;
}

/*
 * Keep a block after the program that owned it has finished, which is what
 * Ptermres asks for. It shrinks to what was asked to be kept and is then never
 * freed and never handed out again.
 *
 * Answers the address the next program can be loaded at, or 0 when there is no
 * such block - a program calling Ptermres about memory that is not its own.
 */
uint32_t mem_keep(uint32_t block, uint32_t keep)
{
    struct mem_area *ma = find_mem_area(block, 0);
    struct mem_area *ptr, *prev;
    uint32_t floor = 0;

    if (!ma)
        return 0;

    /* Asking to keep more than was owned keeps what was owned. TOS had the
     * same arithmetic to do and no more room to do it in. */
    if (keep > ma->len)
        keep = ma->len;

    ma->len = keep;
    ma->resident = 1;

    /*
     * And everything else the program owned goes, which is the other half of
     * what Ptermres means: it keeps what was asked for and releases the rest.
     *
     * It matters more here than it sounds. A program that printed anything has
     * a buffer the C library Malloc'd somewhere above it, and a block left
     * behind in the middle of the machine is a block the next program cannot be
     * loaded across - so without this the memory a resident did not even want
     * decides where everything after it can go.
     *
     * Everything still here belongs to the program that is leaving, because a
     * machine runs one at a time. The exceptions are the blocks that earlier
     * programs kept, and those say so.
     */
    prev = 0;
    ptr = mem_list;

    while (ptr)
    {
        struct mem_area *next = ptr->next;

        if (!ptr->resident)
        {
            if (prev)
                prev->next = next;
            else
                mem_list = next;

            free(ptr);
        }
        else
        {
            /* The next program goes above the highest thing that stayed */
            if (ptr->base + ptr->len > floor)
                floor = ptr->base + ptr->len;

            prev = ptr;
        }

        ptr = next;
    }

    /* Even, because a basepage is read as words and a 68000 takes an address
     * error on an odd one */
    return (floor + 1) & ~1u;
}

void gemdos_mem_init(struct tos_environment *te)
{
    struct mem_area *ma = malloc(sizeof(struct mem_area));
    memset(ma, 0, sizeof(struct mem_area));
    
    /* The initial area is by convention and relates to the binary loading and
     * base page setup from tossystem */
    ma->base = 0x800;
    ma->len = te->tpa_len; /* What the application was given, basepage and all */
    /* The address the memory the emulator has ends at, which is not the same
     * as the top of the initial block: an accessory is given room for itself
     * and the rest of the machine stays free, which is where the stack it
     * Mallocs comes from. A program is given all of it and the two meet. */
    mem_allocatable_top = 0x900 + (uint32_t)te->size;

    mem_list = ma;
}

void gemdos_mem_free()
{
    while (mem_list)
    {
        struct mem_area *n = mem_list->next;
        free(mem_list);
        mem_list = n;
    }
}
