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

#include "gemdos.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "tossystem.h"
#include "cpu.h"
#include "m68k.h"
#include "utils.h"

#define GEMDOS_TRACE_CONTEXT
#include "config.h"

/* GEMDOS return values */

#define GEMDOS_E_OK    (0)

#define GEMDOS_EINVAL  (-25) /* Mint */

#define GEMDOS_EINVFN  (-32)
#define GEMDOS_EFILNF  (-33)
#define GEMDOS_EPTHNF  (-34)
#define GEMDOS_ENHNDL  (-35)
#define GEMDOS_EACCDN  (-36)
#define GEMDOS_EIHNDL  (-37)
#define GEMDOS_ENSMEM  (-39)
#define GEMDOS_EIMBA   (-40)
#define GEMDOS_EDRIVE  (-46)
#define GEMDOS_ECWD    (-47)
#define GEMDOS_ENSAME  (-48)
#define GEMDOS_ENMFIL  (-49)
#define GEMDOS_ELOCKED (-58)
#define GEMDOS_ENSLOCK (-59)
#define GEMDOS_ERANGE  (-64)
#define GEMDOS_EINTRN  (-65)
#define GEMDOS_EPLFMT  (-66)
#define GEMDOS_EGSBF   (-67)
#define GEMDOS_EBREAK  (-68)
#define GEMDOS_EXCPT   (-69)
#define GEMDOS_EPTHOV  (-70)
#define GEMDOS_ELOOP   (-80)
#define GEMDOS_EPIPE   (-81)

/* GEMDOS functions */

/* Console I/O functions *****************************************************/

uint32_t GEMDOS_Cconin()
{   
    FUNC_TRACE_ENTER
    
    return getchar() & 0xff; /* TODO no shift key status, scancode */
}

uint32_t GEMDOS_Cconout()
{
    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x '%c'\n", peek_u16(2), peek_u16(2)&0xff);
    }

    putchar(peek_u16(2)&0xff);
    return 0;
}

uint32_t GEMDOS_Cconis()
{
    FUNC_TRACE_ENTER

    if (console_input_available())
        return -1;
    else
        return 0;
}

uint32_t GEMDOS_Cconos()
{
    FUNC_TRACE_ENTER

    return -1; /* Always ready */
}

uint32_t GEMDOS_Cconws()
{
    uint32_t adr = peek_u32(2);
    uint32_t res = 0;
    uint8_t ch;

    FUNC_TRACE_ENTER {
        printf("    0x%x\n", adr);
    }
    
    while((ch=m68k_read_disassembler_8(adr++)))
    {
        putchar(ch);
        res++;
    }
    
    return res;
}

/* File functions ************************************************************/

uint32_t GEMDOS_Fseek()
{
    /*
     * STDIN       Current File Handle 0 (standard input)
     * STDOUT      Current File Handle 1 (standard output)
     * STDERR      Current File Handle 2 (standard error)
     * 
     * seekmode Type of repositioning:
     *       0 =     From start of file
     *       1 =     From current position
     *       2 =     From end of file
     */
    
    uint16_t seekmode = peek_u16(8);
    uint16_t handle = peek_u16(6);
    uint32_t offset = peek_u32(2);
    off_t ret;
    int whence;
    
    FUNC_TRACE_ENTER_ARGS {
        printf("    offset: 0x%x, handle: 0x%x, seekmode: 0x%x\n", offset, handle, seekmode);
    }
    
    switch (seekmode)
    {
    case 0: /* From start of file */
        whence = SEEK_SET;
        break;
    case 1: /* From current position */
        whence = SEEK_CUR;
        break;
    case 2: /* From end of file */
        whence = SEEK_END;
        break;
    default:
        return GEMDOS_EINVAL;
    }
    
    ret = lseek(handle, offset, whence);
    
    if (ret < 0)
    {
        switch(errno)
        {
        case EBADF:
            return GEMDOS_EIHNDL;
        case ESPIPE:
            return GEMDOS_EACCDN;
        case EINVAL:
            return GEMDOS_EINVAL;
        case EOVERFLOW:
        case ENXIO:
        default:
            return GEMDOS_EINTRN;
        }
    }
    
    return ret;
}

uint32_t GEMDOS_Fdatime()
{
    struct stat buf;
    struct tm *lt;
    int ret;
    uint32_t res;
    
    uint16_t wflag = peek_u16(8);
    uint16_t handle = peek_u16(6);
    uint32_t ptr = peek_u32(2);
    
    if (wflag == 0)
    {
        /* Read time */
        
        ret = fstat(handle, &buf);
        
        if (!ret)
        {
            lt = localtime(&buf.st_mtime);
    
            res = (lt->tm_sec / 2) |
                  (lt->tm_min << 5) |
                  (lt->tm_hour << 11) |
                  (lt->tm_mday << 16) |
                  ((lt->tm_mon+1) << 21) |
                  ((lt->tm_year-80) << 25);

            m68k_write_memory_32(ptr, res);
                  
            return 0;
        }
        else if (ret == EBADF)
            return GEMDOS_EIHNDL;
        else
            return GEMDOS_EINTRN;
    }
    else
        return GEMDOS_EINVAL; /* TODO we do not support setting datime, only reading */
}

uint32_t GEMDOS_Dgetdrv()
{
    return 2; /* C: */
}

/* Process management functions **********************************************/

uint32_t GEMDOS_Pterm()
{
    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", peek_u16(2));
    }

    exit(peek_u16(2));
    return 0;
}
        
uint32_t GEMDOS_Pterm0()
{
    FUNC_TRACE_ENTER

    exit(0);
    return 0;
}

/* Memory management functions ***********************************************/

/* The memory areas are stored in a sorted order by base */
struct mem_area;
struct mem_area {
    uint32_t base, len;
    struct mem_area *next;
};
struct mem_area *mem_list;
uint32_t mem_allocatable_top;

struct mem_area * find_mem_area(uint32_t base, struct mem_area **prevptr)
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

uint32_t GEMDOS_Malloc()
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
    uint32_t prev_top, max_free;
    
    int32_t newsiz = peek_s32(2);
    
    if (newsiz == -1)
    {
        /* Simply locate largest gap */
        
        max_free = 0;

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
        
        if (max_free < mem_allocatable_top - prev_top)
            max_free = mem_allocatable_top - prev_top;
        
        return max_free;
    }
    else
    {    
        prev = mem_list;
        if (prev)
            ptr = mem_list->next;
        else
            ptr = 0;
        
        while (ptr)
        {
            prev_top = prev->base + prev->len;
            if (ptr->base - prev_top > newsiz)
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
        
        if (newsiz < mem_allocatable_top - prev_top)
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

uint32_t GEMDOS_Mfree()
{
    struct mem_area *ma, *prev;
    
    uint32_t block = peek_u32(2);
    
    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", block);
    }
    
    ma = find_mem_area(block, &prev);
    
    if (!ma)
        return GEMDOS_EIMBA;
    
    if (prev)
        prev->next = ma->next;
    else
        mem_list = ma->next;
    
    free(ma);
    
    return 0;
}

/* Date/time functions *******************************************************/

uint32_t GEMDOS_Tgetdate()
{
    /*
    * 0-4     Day (1-31)
    * 5-8     Month (1-12)
    * 9-15    Year (0-119, 0= 1980)
    * 
    * http://toshyp.atari.org/en/00500a.html
    */
    uint32_t res = 0;
    time_t t;
    struct tm *lt;

    FUNC_TRACE_ENTER
    
    t = time(NULL);
    lt = localtime(&t);
    
    res = lt->tm_mday |
          ((lt->tm_mon+1) << 5) |
          ((lt->tm_year-80) << 9);
    
    return res;
}

uint32_t GEMDOS_Tgettime()
{
    /*
    * 0-4     Seconds in units of two (0-29)
    * 5-10    Minutes (0-59)
    * 11-15   Hours (0-23)
    * 
    * http://toshyp.atari.org/en/00500a.html
    */
    uint32_t res = 0;
    time_t t;
    struct tm *lt;
    
    FUNC_TRACE_ENTER

    t = time(NULL);
    lt = localtime(&t);
    
    res = (lt->tm_sec / 2) |
          (lt->tm_min << 5) |
          (lt->tm_hour << 11);
    
    return res;
}

/* Misc functions ************************************************************/

uint32_t GEMDOS_Super()
{
    uint32_t lv0 = peek_u32(2);
    uint32_t res = 0;
 
    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", lv0);
    }
    
    if (lv0 == 0) { /* Set CPU in supervisor mode */
        res = m68k_get_reg(0, M68K_REG_A7);
        enable_supervisor_mode();
    } else if (lv0 == 1) { /* Return 1 if in supervisor mode, otherwise zero */
        if (is_supervisor_mode_enabled()) {
            res = 1;
        } else {
            res = 0;
        }
    } else { /* Set CPU in user mode, set SP to lv0 */
        m68k_set_reg(M68K_REG_USP, lv0);
        disable_supervisor_mode();
        res = 0;
    }
    
    return res;
}    

/* Used to tag Mint-only calls that should not halt execution, but are not implemented */
uint32_t GEMDOS_Unknown();

/* Table of non-implemented GEMDOS functions */

#define GEMDOS_Ssystem GEMDOS_Unknown
#define GEMDOS_Ffstat64 GEMDOS_Unknown
#define GEMDOS_Tgettimeofday GEMDOS_Unknown

#define GEMDOS_Cauxin NULL
#define GEMDOS_Cauxos NULL
#define GEMDOS_Cauxis NULL
#define GEMDOS_Cauxout NULL
#define GEMDOS_Cconrs NULL
#define GEMDOS_Cnecin NULL
#define GEMDOS_Cprnos NULL
#define GEMDOS_Cprnout NULL
#define GEMDOS_Crawcin NULL
#define GEMDOS_Crawio NULL
#define GEMDOS_Dclosedir NULL
#define GEMDOS_Dcntl NULL
#define GEMDOS_Dcreate NULL
#define GEMDOS_Ddelete NULL
#define GEMDOS_Dfree NULL
#define GEMDOS_Dgetcwd NULL
#define GEMDOS_Dgetpath NULL
#define GEMDOS_Dlock NULL
#define GEMDOS_Dopendir NULL
#define GEMDOS_Dpathconf NULL
#define GEMDOS_Dreaddir NULL
#define GEMDOS_Drewinddir NULL
#define GEMDOS_Dsetdrv NULL
#define GEMDOS_Dsetpath NULL
#define GEMDOS_Fattrib NULL
#define GEMDOS_Fchmod NULL
#define GEMDOS_Fchown NULL
#define GEMDOS_Fclose NULL
#define GEMDOS_Fcntl GEMDOS_Unknown
#define GEMDOS_Fcreate NULL
#define GEMDOS_Fdelete NULL
#define GEMDOS_Fdup NULL
#define GEMDOS_Fforce NULL
#define GEMDOS_Fgetchar NULL
#define GEMDOS_Fgetdta NULL
#define GEMDOS_Finstat NULL
#define GEMDOS_Flink NULL
#define GEMDOS_Flock NULL
#define GEMDOS_Fmidipipe NULL
#define GEMDOS_Fopen NULL
#define GEMDOS_Foutstat NULL
#define GEMDOS_Fpipe NULL
#define GEMDOS_Fputchar NULL
#define GEMDOS_Fread NULL
#define GEMDOS_Freadlink NULL
#define GEMDOS_Frename NULL
#define GEMDOS_Fselect NULL
#define GEMDOS_Fsetdta NULL
#define GEMDOS_Fsfirst NULL
#define GEMDOS_Fsnext NULL
#define GEMDOS_Fsymlink NULL
#define GEMDOS_Fwrite NULL
#define GEMDOS_Fxattr NULL
#define GEMDOS_Maddalt NULL
#define GEMDOS_Mxalloc NULL
#define GEMDOS_Pause NULL
#define GEMDOS_Pdomain GEMDOS_Unknown
#define GEMDOS_Pexec NULL
#define GEMDOS_Pfork NULL
#define GEMDOS_Pgetegid GEMDOS_Unknown
#define GEMDOS_Pgeteuid GEMDOS_Unknown
#define GEMDOS_Pgetgid GEMDOS_Unknown
#define GEMDOS_Pgetpgrp NULL
#define GEMDOS_Pgetpid NULL
#define GEMDOS_Pgetppid NULL
#define GEMDOS_Pgetuid GEMDOS_Unknown
#define GEMDOS_Pkill NULL
#define GEMDOS_Pmsg NULL
#define GEMDOS_Pnice NULL
#define GEMDOS_Prenice NULL
#define GEMDOS_Prusage NULL
#define GEMDOS_Psemaphore NULL
#define GEMDOS_Psetgit NULL
#define GEMDOS_Psetlimit NULL
#define GEMDOS_Psetpgrp NULL
#define GEMDOS_Psetuid NULL
#define GEMDOS_Psigaction NULL
#define GEMDOS_Psigblock NULL
#define GEMDOS_Psignal NULL
#define GEMDOS_Psigpause NULL
#define GEMDOS_Psigpending NULL
#define GEMDOS_Psigreturn NULL
#define GEMDOS_Psigsetmask NULL
#define GEMDOS_Ptermres NULL
#define GEMDOS_Pumask NULL
#define GEMDOS_Pursval NULL
#define GEMDOS_Pvfork NULL
#define GEMDOS_Pwait NULL
#define GEMDOS_Pwait3 NULL
#define GEMDOS_Pwaitpid NULL
#define GEMDOS_Salert NULL
#define GEMDOS_Sversion NULL
#define GEMDOS_Pyield NULL
#define GEMDOS_Sysconf NULL
#define GEMDOS_Talarm NULL
#define GEMDOS_Tsetdate NULL
#define GEMDOS_Tsettime NULL

/* GEMDOS function table according to
 * http://www.yardley.cc/atari/compendium/atari-compendium-GEMDOS-Function-Reference.htm
 * 
 * Added Ssystem from http://toshyp.atari.org/en/00500e.html
 */
struct GEMDOS_function {
    char *name;
    uint32_t (*fnct)();
    uint16_t id;
};

struct GEMDOS_function GEMDOS_functions[] = {
    {"Cauxin",      GEMDOS_Cauxin, 0x03},
    {"Cauxis",      GEMDOS_Cauxis, 0x12},
    {"Cauxos",      GEMDOS_Cauxos, 0x13},
    {"Cauxout",     GEMDOS_Cauxout, 0x04},
    {"Cconin",      GEMDOS_Cconin, 0x01},
    {"Cconis",      GEMDOS_Cconis, 0x0B},
    {"Cconos",      GEMDOS_Cconos, 0x10},
    {"Cconout",     GEMDOS_Cconout, 0x02},
    {"Cconrs",      GEMDOS_Cconrs, 0x0A},
    {"Cconws",      GEMDOS_Cconws, 0x09},
    {"Cnecin",      GEMDOS_Cnecin, 0x08},
    {"Cprnos",      GEMDOS_Cprnos, 0x11},
    {"Cprnout",     GEMDOS_Cprnout, 0x05},
    {"Crawcin",     GEMDOS_Crawcin, 0x07},
    {"Crawio",      GEMDOS_Crawio, 0x06},
    {"Dclosedir",   GEMDOS_Dclosedir, 0x12B},
    {"Dcntl",       GEMDOS_Dcntl, 0x130},
    {"Dcreate",     GEMDOS_Dcreate, 0x39},
    {"Ddelete",     GEMDOS_Ddelete, 0x3A},
    {"Dfree",       GEMDOS_Dfree, 0x36},
    {"Dgetcwd",     GEMDOS_Dgetcwd, 0x13B},
    {"Dgetdrv",     GEMDOS_Dgetdrv, 0x19},
    {"Dgetpath",    GEMDOS_Dgetpath, 0x47},
    {"Dlock",       GEMDOS_Dlock, 0x135},
    {"Dopendir",    GEMDOS_Dopendir, 0x128},
    {"Dpathconf",   GEMDOS_Dpathconf, 0x124},
    {"Dreaddir",    GEMDOS_Dreaddir, 0x129},
    {"Drewinddir",  GEMDOS_Drewinddir, 0x12A},
    {"Dsetdrv",     GEMDOS_Dsetdrv, 0x0E},
    {"Dsetpath",    GEMDOS_Dsetpath, 0x3B},
    {"Fattrib",     GEMDOS_Fattrib, 0x43},
    {"Fchmod",      GEMDOS_Fchmod, 0x132},
    {"Fchown",      GEMDOS_Fchown, 0x131},
    {"Fclose",      GEMDOS_Fclose, 0x3E},
    {"Fcntl",       GEMDOS_Fcntl, 0x104},
    {"Fcreate",     GEMDOS_Fcreate, 0x3C},
    {"Fdatime",     GEMDOS_Fdatime, 0x57},
    {"Fdelete",     GEMDOS_Fdelete, 0x41},
    {"Fdup",        GEMDOS_Fdup, 0x45},
    {"Fforce",      GEMDOS_Fforce, 0x46},
    {"Fgetchar",    GEMDOS_Fgetchar, 0x107},
    {"Fgetdta",     GEMDOS_Fgetdta, 0x2F},
    {"Finstat",     GEMDOS_Finstat, 0x105},
    {"Flink",       GEMDOS_Flink, 0x12D},
    {"Flock",       GEMDOS_Flock, 0x5C},
    {"Fmidipipe",   GEMDOS_Fmidipipe, 0x126},
    {"Fopen",       GEMDOS_Fopen, 0x3D},
    {"Foutstat",    GEMDOS_Foutstat, 0x106},
    {"Fpipe",       GEMDOS_Fpipe, 0x100},
    {"Fputchar",    GEMDOS_Fputchar, 0x108},
    {"Fread",       GEMDOS_Fread, 0x3F},
    {"Freadlink",   GEMDOS_Freadlink, 0x12F},
    {"Frename",     GEMDOS_Frename, 0x56},
    {"Fseek",       GEMDOS_Fseek, 0x42},
    {"Fselect",     GEMDOS_Fselect, 0x11D},
    {"Fsetdta",     GEMDOS_Fsetdta, 0x1A},
    {"Fsfirst",     GEMDOS_Fsfirst, 0x4E},
    {"Fsnext",      GEMDOS_Fsnext, 0x4F},
    {"Fsymlink",    GEMDOS_Fsymlink, 0x12E},
    {"Fwrite",      GEMDOS_Fwrite, 0x40},
    {"Fxattr",      GEMDOS_Fxattr, 0x12C},
    {"Maddalt",     GEMDOS_Maddalt, 0x14},
    {"Malloc",      GEMDOS_Malloc, 0x48},
    {"Mfree",       GEMDOS_Mfree, 0x49},
    {"Mshrink",     GEMDOS_Mshrink, 0x4A},
    {"Mxalloc",     GEMDOS_Mxalloc, 0x44},
    {"Pause",       GEMDOS_Pause, 0x121},
    {"Pdomain",     GEMDOS_Pdomain, 0x119},
    {"Pexec",       GEMDOS_Pexec, 0x4B},
    {"Pfork",       GEMDOS_Pfork, 0x11B},
    {"Pgetegid",    GEMDOS_Pgetegid, 0x139},
    {"Pgeteuid",    GEMDOS_Pgeteuid, 0x138},
    {"Pgetgid",     GEMDOS_Pgetgid, 0x114},
    {"Pgetpgrp",    GEMDOS_Pgetpgrp, 0x10D},
    {"Pgetpid",     GEMDOS_Pgetpid, 0x10B},
    {"Pgetppid",    GEMDOS_Pgetppid, 0x10C},
    {"Pgetuid",     GEMDOS_Pgetuid, 0x10F},
    {"Pkill",       GEMDOS_Pkill, 0x111},
    {"Pmsg",        GEMDOS_Pmsg, 0x125},
    {"Pnice",       GEMDOS_Pnice, 0x10A},
    {"Prenice",     GEMDOS_Prenice, 0x127},
    {"Prusage",     GEMDOS_Prusage, 0x11E},
    {"Psemaphore",  GEMDOS_Psemaphore, 0x134},
    {"Psetgit",     GEMDOS_Psetgit, 0x115},
    {"Psetlimit",   GEMDOS_Psetlimit, 0x11F},
    {"Psetpgrp",    GEMDOS_Psetpgrp, 0x10E},
    {"Psetuid",     GEMDOS_Psetuid, 0x110},
    {"Psigaction",  GEMDOS_Psigaction, 0x137},
    {"Psigblock",   GEMDOS_Psigblock, 0x116},
    {"Psignal",     GEMDOS_Psignal, 0x112},
    {"Psigpause",   GEMDOS_Psigpause, 0x136},
    {"Psigpending", GEMDOS_Psigpending, 0x123},
    {"Psigreturn",  GEMDOS_Psigreturn, 0x11A},
    {"Psigsetmask", GEMDOS_Psigsetmask, 0x117},
    {"Pterm",       GEMDOS_Pterm, 0x4C},
    {"Pterm0",      GEMDOS_Pterm0, 0x0},
    {"Ptermres",    GEMDOS_Ptermres, 0x31},
    {"Pumask",      GEMDOS_Pumask, 0x133},
    {"Pursval",     GEMDOS_Pursval, 0x118},
    {"Pvfork",      GEMDOS_Pvfork, 0x113},
    {"Pwait",       GEMDOS_Pwait, 0x109},
    {"Pwait3",      GEMDOS_Pwait3, 0x11C},
    {"Pwaitpid",    GEMDOS_Pwaitpid, 0x13A},
    {"Salert",      GEMDOS_Salert, 0x13C},
    {"Super",       GEMDOS_Super, 0x20},
    {"Sversion",    GEMDOS_Sversion, 0x30},
    {"Pyield",      GEMDOS_Pyield, 0xFF},
    {"Sysconf",     GEMDOS_Sysconf, 0x122},
    {"Talarm",      GEMDOS_Talarm, 0x120},
    {"Tgetdate",    GEMDOS_Tgetdate, 0x2A},
    {"Tgettime",    GEMDOS_Tgettime, 0x2C},
    {"Tsetdate",    GEMDOS_Tsetdate, 0x2B},
    {"Tsettime",    GEMDOS_Tsettime, 0x2D},
    {"Ssystem",     GEMDOS_Ssystem, 0x154},
    {"Ffstat64",    GEMDOS_Ffstat64, 0x15D},
    {"Tgettimeofday", GEMDOS_Tgettimeofday, 0x155}
};

void gemdos_init(struct tos_environment *te)
{
    struct mem_area *ma = malloc(sizeof(struct mem_area));
    memset(ma, 0, sizeof(struct mem_area));
    
    /* The initial area is by convention and relates to the binary loading and
     * base page setup from tossystem */
    ma->base = 0x800; 
    ma->len = te->size + 0x100; /* Size + basepage */
    mem_allocatable_top = ma->len;
    
    mem_list = ma;
}

void gemdos_free()
{
    while (mem_list)
    {
        struct mem_area *n = mem_list->next;
        free(mem_list);
        mem_list = n;
    }
}

void gemdos_trap()
{
    uint16_t fnct = peek_u16(0);
    int i;
    
    for(i=0; i<=sizeof(GEMDOS_functions)/sizeof(struct GEMDOS_function); ++i) {
        if (GEMDOS_functions[i].id == fnct) {
            if (GEMDOS_functions[i].fnct) {
                m68k_set_reg(M68K_REG_D0, GEMDOS_functions[i].fnct());
            } else {
                halt_execution();
                printf("GEMDOS %s (0x%x) not implemented\n", GEMDOS_functions[i].name, fnct);
            }
            
            return;
        }
    }
            
    halt_execution();
    printf("GEMDOS Unknown function called 0x%x\n", fnct);
}

/* Special function implementations */
uint32_t GEMDOS_Unknown()
{
    int i;
    uint16_t fnct;
    
    FUNC_TRACE_ENTER_ARGS {
        fnct = peek_u16(0);
        printf("    func: 0x%x\n", fnct);

        for(i=0; i<=sizeof(GEMDOS_functions)/sizeof(struct GEMDOS_function); ++i)
            if (GEMDOS_functions[i].id == fnct)
                if (GEMDOS_functions[i].fnct)
                    printf("    %s\n", GEMDOS_functions[i].name);        
    }

    return GEMDOS_EINVFN; /* http://toshyp.atari.org/en/005003.html */
}
