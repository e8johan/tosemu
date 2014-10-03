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

#include "gemdosfile_p.h"

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <time.h>
#include <linux/limits.h>
#include <string.h>
#include <stdlib.h>

#include "m68k.h"
#include "cpu.h"

#include "gemdos_p.h"

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

uint32_t dta_adr;

uint32_t GEMDOS_Fgetdta()
{
    FUNC_TRACE_ENTER
    
    return dta_adr;
}

uint32_t GEMDOS_Fsetdta()
{
    uint32_t adr = peek_u32(2);
    
    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", adr);
    }
    
    dta_adr = adr;
    
    return 0;
}

static int path_from_tos(char *tp, char *up)
{
    /* \ -> / */
    /* Prepend prefix */
    /* Make canonical */
    /* Ensure within prefix */
    
    
    char tbuf[PATH_MAX+1];
    int len;
    char *src, *dest;
    int prev_slash = 1;
    
    memset(tbuf, 0, PATH_MAX+1);
    
    strncpy(up, "/home/e8johan/tos/", PATH_MAX);
    len = strlen(up);
    src = tp;
    dest = up + len;
    
    while(*src && len < PATH_MAX)
    {
        switch(*src)
        {
        case '\\':
            if (!prev_slash)
            {
                *dest = '/';
                ++ dest;
            }
            prev_slash = 1;
            
            break;
        default:
            *dest = *src;
            ++ dest;
            
            prev_slash = 0;

            break;
        }
        
        ++ src;
        ++ len;
    }
    
    realpath(up, tbuf);
    /* TODO work here */
    printf("%d\n", strncmp(up, tbuf, 5+8+4+1));
    if (strncmp(up, tbuf, 18))
        return 0;
    
    return strlen(up);
}

uint32_t GEMDOS_Fsfirst()
{
    char buf[PATH_MAX+1];
    char ubuf[PATH_MAX+1];
    int i;
    
    uint16_t attr = peek_u16(6);
    uint32_t filename = peek_u32(2);
    
    FUNC_TRACE_ENTER_ARGS {
        printf("    filename: 0x%x, attr: 0x%x\n", filename, attr);
    }
    
    memset(buf, 0, PATH_MAX+1);
    memset(ubuf, 0, PATH_MAX+1);
    
    i=1;
    buf[0] = m68k_read_disassembler_8(filename);
    
    while(buf[i-1] && i<PATH_MAX)
    {
        buf[i] = m68k_read_disassembler_8(filename+i);
        ++i;
    }
    
    if (!path_from_tos(buf, ubuf))
        return GEMDOS_EFILNF;
    
    printf("FILENAME: '%s' => '%s'\n", buf, ubuf);
    
    /* TODO continue implementing here, right now we pretend never to find any files */
    
    /* Find the file path */
    /* Do a diropen */
    /* Populate (initialize) the dta */
    /* Decide if we want the opendir ref as a part of the m68k ram DTA, or outside */
    
    return GEMDOS_EFILNF;
}

void gemdos_file_init(struct tos_environment *te)
{
    dta_adr = 0x000830; /* TODO this is probably cheating, points to reserved memory */
}

void gemdos_file_free()
{
}
