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

#include "gemdosfile_p.h"

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <linux/limits.h>
#include <time.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <glob.h>
#include <libgen.h>

#include "m68k.h"
#include "cpu.h"
#include "utils.h"
#include "memory.h"

#include "gemdos_p.h"
#include "gemdosdrive_p.h"

/* File structures ***********************************************************/

#pragma pack(push,2)
struct DTA
{
    uint8_t   d_reserved[21];  /* Reserved for GEMDOS */
    uint8_t   d_attrib;        /* File attributes     */
    uint16_t  d_time;          /* Time                */
    uint16_t  d_date;          /* Date                */
    uint32_t  d_length;        /* File length         */
    int8_t    d_fname[14];     /* Filename            */
};
#pragma pack(pop)

/*
 * The handle table is the process' list of open files. Fdup and Fforce both
 * make a second handle refer to a file another handle already has open, so the
 * open file lives in a structure of its own, counting the handles pointing at
 * it. Closing one of them then leaves the file open for the others.
 */
struct openfile
{
    FILE *f;
    int refs;
};

struct fhandle
{
    struct openfile *of;
    uint32_t flags;
};

/* Handles 0-5 are the standard handles every process starts with, and are the
 * only ones Fforce may redirect. GEMDOS lets a process open 40 files on top of
 * those. */
#define STD_HANDLES 6
#define HANDLES (STD_HANDLES+40)
#define HANDLE_ALLOCATED 0x001

#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_LABEL      0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20

static struct tos_environment *tos_env;

static struct fhandle handles[HANDLES];

static int invalid_handle(uint16_t h)
{
    return (h >= HANDLES) || !(handles[h].flags & HANDLE_ALLOCATED)
           || (handles[h].of == NULL);
}

/* The stream a handle refers to. Only call this once invalid_handle said no. */
static FILE *handle_file(uint16_t h)
{
    return handles[h].of->f;
}

static struct openfile *open_stream(FILE *f)
{
    struct openfile *of = malloc(sizeof *of);

    if (of == NULL)
        return NULL;

    of->f = f;
    of->refs = 1;

    return of;
}

/*
 * Drops one handle's reference to a file, closing it once no handle is left.
 *
 * The streams tosemu inherited from the host outlive the application, so they
 * are unlinked from the table but never closed.
 */
static void release_stream(struct openfile *of)
{
    if (of == NULL || --of->refs > 0)
        return;

    if (of->f != stdin && of->f != stdout && of->f != stderr)
        fclose(of->f);

    free(of);
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
    int32_t offset = peek_s32(2);
    long ret;
    int whence;

    FUNC_TRACE_ENTER_ARGS {
        printf("    offset: 0x%x, handle: 0x%x, seekmode: 0x%x\n", offset, handle, seekmode);
    }

    if (invalid_handle(handle))
        return GEMDOS_EIHNDL;

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

    errno = 0;
    if (fseek(handle_file(handle), offset, whence) != 0)
    {
        /* A failed seek must not leave the error indicator set, or every
         * later read or write on the handle would be reported as failed. */
        clearerr(handle_file(handle));

        switch(errno)
        {
        case EBADF:
            return GEMDOS_EIHNDL;
        case ESPIPE:
            /* MiNTLib derives errno as -<GEMDOS code>, and its stdio only
             * accepts ESPIPE as "this handle has no position". Anything else
             * makes it drop the buffered data instead of writing it. */
            return GEMDOS_ESPIPE;
        case EINVAL:
            return GEMDOS_EINVAL;
        case EOVERFLOW:
        case ENXIO:
        default:
            return GEMDOS_EINTRN;
        }
    }

    /* Fseek returns the resulting absolute position in the file */
    ret = ftell(handle_file(handle));
    if (ret < 0)
        return GEMDOS_EINTRN;

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

    FUNC_TRACE_ENTER_ARGS {
        printf("    handle: 0x%x, wflag: %d, ptr: 0x%x\n", handle, wflag, ptr);
    }

    if (invalid_handle(handle))
        return GEMDOS_EIHNDL;

    if (wflag == 0)
    {
        /* Read time */

        ret = fstat(fileno(handle_file(handle)), &buf);

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
        else
            return GEMDOS_EINTRN;
    }
    else
        return GEMDOS_EINVAL; /* TODO we do not support setting datime, only reading */
}

uint32_t GEMDOS_Dgetdrv()
{
    FUNC_TRACE_ENTER

    return drive_current();
}

uint32_t GEMDOS_Dsetdrv()
{
    uint16_t drive = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    drive: %d\n", drive);
    }

    /* TOS ignores a request for a drive that is not there, and always answers
     * with the drive map, http://toshyp.atari.org/en/00500b.html */
    drive_set_current(drive);

    return drive_map();
}

uint32_t dta_addr;

uint32_t GEMDOS_Fgetdta()
{
    FUNC_TRACE_ENTER
    
    return dta_addr;
}

uint32_t GEMDOS_Fsetdta()
{
    uint32_t addr = peek_u32(2);
    
    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", addr);
    }
        
    dta_addr = addr;
    
    return 0;
}

static int get_path(char *buf, uint32_t address)
{
    int i=1;
    buf[0] = m68k_read_disassembler_8(address);
    
    while(buf[i-1] && i<PATH_MAX)
    {
        buf[i] = m68k_read_disassembler_8(address+i);
        ++i;
    }

    return i;
}

uint32_t GEMDOS_Dgetpath()
{
    uint32_t addr = peek_u32(2);
    uint16_t drive = peek_u16(6);
    char ubuf[PATH_MAX+1];
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    addr: 0x%x, drive=%d\n", addr, drive);
    }

    /* Drive 0 means the current drive, anything else names one directly */
    if (drive != 0 && !drive_volume(drive - 1))
        return GEMDOS_EDRIVE;

    memset(ubuf, 0, PATH_MAX+1);
    getcwd(ubuf, sizeof ubuf);

    i=0;
    do
    {
        m68k_write_memory_8(addr+i, ubuf[i]);
        ++i;
    }
    while(ubuf[i-1]!=0 && i<PATH_MAX);

    return 0;
}

/*
 * TOS file systems are case insensitive, while most host file systems are not.
 * Programs therefore happily ask for FILE.TXT when the host knows the file as
 * file.txt - Gen for instance upper cases every include file name.
 *
 * Resolve the host path in place, replacing every component that does not
 * exist verbatim with one that only differs in case, if the host has such a
 * component. Components without a match are left untouched, so that Fcreate
 * and Dcreate still create names exactly as the application spelled them.
 */
static void resolve_case(char *path)
{
    char *cur, *sep;
    DIR *dir;
    struct dirent *de;

    if (access(path, F_OK) == 0)
        return; /* The path exists exactly as spelled */

    cur = path;
    if (*cur == '/')
        ++cur; /* The root is always spelled correctly */

    while (*cur)
    {
        sep = strchr(cur, '/');
        if (sep == cur)
        {
            /* Empty component, e.g. from a doubled separator */
            ++cur;
            continue;
        }

        if (sep)
            *sep = 0;

        if (access(path, F_OK) != 0)
        {
            if (cur == path)
                dir = opendir(".");
            else if (cur == path + 1)
                dir = opendir("/");
            else
            {
                cur[-1] = 0;
                dir = opendir(path);
                cur[-1] = '/';
            }

            if (dir)
            {
                while ((de = readdir(dir)) != NULL)
                {
                    if (strcasecmp(de->d_name, cur) == 0)
                    {
                        /* A case insensitive match has the same length */
                        strcpy(cur, de->d_name);
                        break;
                    }
                }
                closedir(dir);
            }
        }

        if (!sep)
            break;

        *sep = '/';
        cur = sep + 1;
    }
}

/*
 * Converts a TOS path, without its drive prefix, to a host path
 *
 * Returns 0 on success, or a negative GEMDOS error.
 */
static int32_t host_resolve(const char *tp, char *up)
{
    char tbuf[PATH_MAX+1];
    int len;
    const char *src;
    char *dest;
    int prev_slash = 1;

    memset(tbuf, 0, PATH_MAX+1);

    /* Prepend prefix */
    strncpy(up, tos_env->base_path, PATH_MAX);
    len = strlen(up);
    src = tp;
    dest = up + len;

    /* Convert \ -> / */
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

    resolve_case(up);

    /* Make canonical */ /* TODO, this limits the usage of symbolic links when mixing the TOS and host file systems */
    realpath(up, tbuf);

    if (tos_env->base_path[0] != 0)
    {
        /* Ensure within prefix */
        if (strncmp(up, tbuf, strlen(tos_env->base_path)-1))
            return GEMDOS_EFILNF;
    }

    return GEMDOS_E_OK;
}

/* The host file system never has its media swapped */
static int host_mediach(void)
{
    return 0;
}

static struct volume host_volume = {
    "host",
    host_resolve,
    host_mediach
};

/*
 * Converts a TOS path to a host path, resolving the drive it refers to
 *
 * Returns 0 on success, or a negative GEMDOS error.
 */
static int32_t path_from_tos(char *tp, char *up)
{
    const char *rest;
    struct volume *v;
    int drive;

    drive = drive_from_path(tp, &rest);
    if (drive < 0)
        return GEMDOS_EDRIVE;

    v = drive_volume(drive);

    return v->resolve(rest, up);
}

uint32_t GEMDOS_Dsetpath()
{
    uint32_t addr = peek_u32(2);
    char buf[PATH_MAX+1];
    char ubuf[PATH_MAX+1];
    int32_t err;

    FUNC_TRACE_ENTER_ARGS {
        printf("    addr: 0x%x\n", addr);
    }

    memset(buf, 0, PATH_MAX+1);
    memset(ubuf, 0, PATH_MAX+1);
    get_path(buf, addr);

    err = path_from_tos(buf, ubuf);
    if (err)
        return err;

    if (chdir(ubuf))
        perror("chdir");

    return 0;
}

uint32_t GEMDOS_Dcreate()
{
    uint32_t addr = peek_u32(2);
    char buf[PATH_MAX+1];
    char ubuf[PATH_MAX+1];
    int32_t err;

    FUNC_TRACE_ENTER_ARGS {
        printf("    addr: 0x%x\n", addr);
    }

    memset(buf, 0, PATH_MAX+1);
    memset(ubuf, 0, PATH_MAX+1);
    get_path(buf, addr);

    err = path_from_tos(buf, ubuf);
    if (err)
        return err;

    if(mkdir(ubuf, 0777) != 0)
        return GEMDOS_EACCDN;

    return 0;
}

uint32_t GEMDOS_Fdelete()
{
    uint32_t addr = peek_u32(2);
    char buf[PATH_MAX+1];
    char ubuf[PATH_MAX+1];
    int32_t err;

    FUNC_TRACE_ENTER_ARGS {
        printf("    addr: 0x%x\n", addr);
    }

    memset(buf, 0, PATH_MAX+1);
    memset(ubuf, 0, PATH_MAX+1);
    get_path(buf, addr);

    err = path_from_tos(buf, ubuf);
    if (err)
        return err;

    if (unlink(ubuf) != 0)
        return GEMDOS_EFILNF;

    return 0;
}

/* Opening a file never hands out one of the standard handles, so the search
 * starts past them even when the application has closed one. */
static int get_handle(struct openfile *of)
{
    int i;
    for (i = STD_HANDLES; i < HANDLES; i++)
    {
        if (handles[i].flags & HANDLE_ALLOCATED)
            continue;
        handles[i].of = of;
        handles[i].flags = HANDLE_ALLOCATED;
        return i;
    }
    return -1;
}

static void make_dirs(char *path)
{
    char *start, *end;

    start = path;
    while ((end = strchr(start, '/')) != NULL)
    {
        *end = 0;
        /* Skip the root itself, and any empty component from a doubled '/' */
        if (end != path && mkdir(path, 0777) < 0 && errno != EEXIST)
            perror("mkdir");
        *end = '/';
        start = end + 1;
    }
}

uint32_t GEMDOS_Fcreate()
{
    uint32_t addr = peek_u32(2);
    char buf[PATH_MAX+1];
    char ubuf[PATH_MAX+1];
    int32_t err;
    int h, fd;
    struct openfile *of;

    FUNC_TRACE_ENTER_ARGS {
        printf("    addr: 0x%x\n", addr);
    }

    memset(buf, 0, PATH_MAX+1);
    memset(ubuf, 0, PATH_MAX+1);
    get_path(buf, addr);

    err = path_from_tos(buf, ubuf);
    if (err)
        return err;

    FUNC_TRACE_ARGS {
        printf("    path: '%s' -> '%s'\n", buf, ubuf);
    }

    make_dirs(ubuf);
    fd = creat(ubuf, 0777);
    if (fd < 0)
        return GEMDOS_EACCDN;

    of = open_stream(fdopen(fd, "w"));
    if (of == NULL)
        return GEMDOS_ENSMEM;

    h = get_handle(of);
    if (h == -1)
    {
        release_stream(of);
        return GEMDOS_ENHNDL;
    }

    return h;
}

struct globitem;
struct globitem {
    glob_t *g;
    int id;
    
    struct globitem *next;
};

struct globitem *globhead = 0;

glob_t *gemdos_prepare_dta(int *id)
{
    static int sid = 42;
    glob_t *res;
    struct globitem *item;
    
    sid ++;
    res = malloc(sizeof(glob_t));
    item = malloc(sizeof(struct globitem));
    
    item->g = res;
    item->id = sid;
    item->next = globhead;
    globhead = item;
    
    *id = sid;
    return res;
}

glob_t *gemdos_find_dta(int *id)
{
    struct globitem *ptr = globhead;
    while (ptr) {
        if (ptr->id == *id)
            return ptr->g;
        ptr = ptr->next;
    }
    
    return 0;
}

/* TODO where would be a good place to call this? Fsnext? */
void gemdos_clear_dta(int *id)
{
    struct globitem *ptr, *pptr;
    
    pptr = 0;
    ptr = globhead;
    while (ptr) {
        if (ptr->id == *id) {
            if (pptr)
                pptr->next = ptr->next;
            else
                globhead = ptr->next;
            
            globfree(ptr->g);
            free(ptr);
            
            return;
        }
        pptr = ptr;
        ptr = ptr->next;
    }
}

static uint16_t mode_to_attrib(mode_t mode)
{
  uint16_t attrib = 0;

  if (S_ISDIR(mode))
      attrib |= ATTR_DIRECTORY;

  return attrib;
}

uint32_t GEMDOS_Fsfirst()
{
    glob_t *gres;
    struct stat sres;
    struct tm *lt;

    char buf[PATH_MAX+1];
    char ubuf[PATH_MAX+1];
    int32_t err;

    int i;
    int gres_id;
    char *bn;

    struct DTA *dta;

    uint32_t filename = peek_u32(2);
    uint16_t attr = peek_u16(6);
    
    FUNC_TRACE_ENTER_ARGS {
        printf("    filename: 0x%x, attr: 0x%x\n", filename, attr);
    }
    
    memset(buf, 0, PATH_MAX+1);
    memset(ubuf, 0, PATH_MAX+1);
    
    gres = gemdos_prepare_dta(&gres_id);
    
    get_path(buf, filename);
    
    err = path_from_tos(buf, ubuf);
    if (err)
        return err;
    
    /* TODO, take attr into account */
    
    if ((i = glob(ubuf, 0, 0, gres)) == 0) {
        if (gres->gl_pathc>0) {
            stat(gres->gl_pathv[0], &sres);
            lt = localtime(&sres.st_mtime);
            
            dta = (struct DTA*)(tos_mem_to_host_mem(dta_addr));
            
            ((int*)dta)[0] = gres_id;
            ((int*)dta)[1] = 0;
            
            /* 
            Bit 0:  File is write-protected
            Bit 1:  File is hidden
            Bit 2:  System file
            Bit 3:  Volume label (diskette name)
            Bit 4:  Directory
            Bit 5:  Archive bit 
            */
            dta->d_attrib = mode_to_attrib(sres.st_mode);
            
            /*
            0-4 Seconds in units of two (0-29)
            5-10    Minutes (0-59)
            11-15   Hours (0-23)
            */
            dta->d_time = endianize_16(
                            (lt->tm_sec / 2) |
                            (lt->tm_min << 5) |
                            (lt->tm_hour << 11));
            
            /*
            0-4 Day (1-31)
            5-8     Month (1-12)
            9-15    Year (0-119, 0= 1980)
            */
            dta->d_date = endianize_16(
                            lt->tm_mday |
                          ((lt->tm_mon+1) << 5) |
                          ((lt->tm_year-80) << 9));
            
            dta->d_length = endianize_32(sres.st_size);
            
            bn = basename(gres->gl_pathv[0]);
            memset(dta->d_fname, 0, 14);
            strncpy((char*)dta->d_fname, bn, 13);
        }
    } else {
        switch(i)
        {
            case GLOB_NOSPACE:
            case GLOB_ABORTED:
            case GLOB_NOMATCH:
                return GEMDOS_EFILNF;
            default:
                return GEMDOS_EFILNF;
        }
    }
    
    return GEMDOS_E_OK;
}

uint32_t GEMDOS_Fsnext()
{
    glob_t *gres;
    struct stat sres;
    struct tm *lt;

    int i;
    char *bn;

    struct DTA *dta;

    FUNC_TRACE_ENTER
    
    dta = (struct DTA*)(tos_mem_to_host_mem(dta_addr));
    gres = gemdos_find_dta((int*)dta);
    i = ((int*)dta)[1] + 1;
    ((int*)dta)[1] = i;
    
    /* TODO, take attr into account */

    if (i < gres->gl_pathc) {
        stat(gres->gl_pathv[i], &sres);
        lt = localtime(&sres.st_mtime);
            
        /* 
        Bit 0:  File is write-protected
        Bit 1:  File is hidden
        Bit 2:  System file
        Bit 3:  Volume label (diskette name)
        Bit 4:  Directory
        Bit 5:  Archive bit 
        */
        dta->d_attrib = mode_to_attrib(sres.st_mode);
        
        /*
        0-4 Seconds in units of two (0-29)
        5-10    Minutes (0-59)
        11-15   Hours (0-23)
        */
        dta->d_time = endianize_16(
                        (lt->tm_sec / 2) |
                        (lt->tm_min << 5) |
                        (lt->tm_hour << 11));
        
        /*
        0-4 Day (1-31)
        5-8     Month (1-12)
        9-15    Year (0-119, 0= 1980)
        */
        dta->d_date = endianize_16(
                        lt->tm_mday |
                        ((lt->tm_mon+1) << 5) |
                        ((lt->tm_year-80) << 9));
        
        dta->d_length = endianize_32(sres.st_size);
        
        bn = basename(gres->gl_pathv[i]);
        memset(dta->d_fname, 0, 14);
        strncpy((char*)dta->d_fname, bn, 13);
    }
    else
    {
        return GEMDOS_ENMFIL;
    }

            
    return GEMDOS_E_OK;   
}

uint32_t GEMDOS_Fopen()
{
    char buf[PATH_MAX+1];
    char ubuf[PATH_MAX+1];
    int32_t err;

    const char *m;
    FILE *f;
    struct openfile *of;
    int h;

    uint32_t filename = peek_u32(2);
    uint16_t mode = peek_u16(6);

    FUNC_TRACE_ENTER_ARGS {
        printf("    filename: 0x%x, mode: 0x%x\n", filename, mode);
    }

    memset(buf, 0, PATH_MAX+1);
    memset(ubuf, 0, PATH_MAX+1);
    
    get_path(buf, filename);

    err = path_from_tos(buf, ubuf);
    if (err)
        return err;

    switch(mode & 0x3)
    {
    case 0:
        m = "r";
        break;
    case 1:
        m = "w";
        break;
    case 2:
        m = "r+";
        break;
    case 3:
        return GEMDOS_EINVAL;
        break;
    }

    FUNC_TRACE_ARGS {
        printf("    path: '%s' -> '%s'\n", buf, ubuf);
    }

    f = fopen(ubuf, m);
    if (f == NULL)
        return GEMDOS_EFILNF;

    of = open_stream(f);
    if (of == NULL)
    {
        fclose(f);
        return GEMDOS_ENSMEM;
    }

    h = get_handle(of);
    if (h == -1)
    {
        release_stream(of);
        return GEMDOS_ENHNDL;
    }

    return h;
}

uint32_t GEMDOS_Fdup()
{
    uint16_t h = peek_u16(2);
    int n;

    FUNC_TRACE_ENTER_ARGS {
        printf("    handle: %d\n", h);
    }

    if (invalid_handle(h))
        return GEMDOS_EIHNDL;

    n = get_handle(handles[h].of);
    if (n == -1)
        return GEMDOS_ENHNDL;

    handles[h].of->refs++;

    return n;
}

uint32_t GEMDOS_Fforce()
{
    uint16_t stdh = peek_u16(2);
    uint16_t h = peek_u16(4);
    struct openfile *of;

    FUNC_TRACE_ENTER_ARGS {
        printf("    stdh: %d, handle: %d\n", stdh, h);
    }

    /* Only the handles a process starts out with can be redirected */
    if (stdh >= STD_HANDLES)
        return GEMDOS_EIHNDL;

    if (invalid_handle(h))
        return GEMDOS_EIHNDL;

    /* Claim the new file before letting go of the old one, so that redirecting
     * a handle onto itself does not close the file in between */
    of = handles[h].of;
    of->refs++;

    release_stream(handles[stdh].of);

    handles[stdh].of = of;
    handles[stdh].flags = HANDLE_ALLOCATED;

    return GEMDOS_E_OK;
}

uint32_t GEMDOS_Fattrib()
{
    struct stat st;
    char buf[PATH_MAX+1];
    char ubuf[PATH_MAX+1];
    int32_t err;

    uint32_t filename = peek_u32(2);
    uint16_t wflag = peek_u16(6);
    uint16_t attr = peek_u16(8);

    FUNC_TRACE_ENTER_ARGS {
        printf("    filename: 0x%x, wflag: %d, attr: 0x%x\n",
               filename, wflag, attr);
    }

    memset(buf, 0, PATH_MAX+1);
    memset(ubuf, 0, PATH_MAX+1);
    
    get_path(buf, filename);

    err = path_from_tos(buf, ubuf);
    if (err)
        return err;

    if (stat(ubuf, &st) < 0)
        return GEMDOS_EFILNF;

    return mode_to_attrib(st.st_mode);
}

uint32_t GEMDOS_Fclose()
{
    uint16_t h = peek_u16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    handle: %d\n", h);
    }

    if (invalid_handle(h))
        return GEMDOS_EIHNDL;

    handles[h].flags = 0;
    release_stream(handles[h].of);
    handles[h].of = NULL;

    return GEMDOS_E_OK;
}

uint32_t GEMDOS_Fread()
{
    uint16_t h = peek_u16(2);
    uint32_t len = peek_u32(4);
    uint32_t buf = peek_u32(8);
    uint8_t *tmp;
    size_t n;
    int i;

    if (invalid_handle(h))
        return GEMDOS_EIHNDL;

    tmp = malloc(len);
    if (tmp == NULL)
        return GEMDOS_ENSMEM;

    /* The error indicator is sticky, so clear it to make sure that what we
     * look at afterwards was caused by this read alone. */
    errno = 0;
    clearerr(handle_file(h));

    n = fread(tmp, 1, len, handle_file(h));
    if (ferror(handle_file(h)))
    {
        int err = errno;

        clearerr(handle_file(h));
        free(tmp);
        return err == EBADF ? GEMDOS_EIHNDL : GEMDOS_EINTRN;
    }

    for (i = 0; i < n; i++)
        m68k_write_memory_8(buf+i, tmp[i]);

    free(tmp);
    return n;
}

uint32_t GEMDOS_Fwrite()
{
    uint16_t h = peek_u16(2);
    uint32_t len = peek_u32(4);
    uint32_t buf = peek_u32(8);
    uint8_t *tmp;
    size_t n;
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    handle: %d, len: %d, buf: %x\n", h, len , buf);
    }

    if (invalid_handle(h))
        return GEMDOS_EIHNDL;

    tmp = malloc(len);
    if (tmp == NULL)
        return GEMDOS_ENSMEM;

    for (i = 0; i < len; i++)
        tmp[i] = m68k_read_memory_8(buf+i);

    /* The error indicator is sticky, so clear it to make sure that what we
     * look at afterwards was caused by this write alone. */
    errno = 0;
    clearerr(handle_file(h));

    n = fwrite(tmp, 1, len, handle_file(h));
    if (ferror(handle_file(h)))
    {
        int err = errno;

        clearerr(handle_file(h));
        free(tmp);
        return err == EBADF ? GEMDOS_EIHNDL : GEMDOS_EINTRN;
    }

    free(tmp);
    return n;
}

void gemdos_file_init(struct tos_environment *te)
{
    int i;

    /* TOS defaults the DTA to the command line in the basepage */
    dta_addr = 0x000880;

    /* tosemu presents the host file system as C: */
    drive_register(DRIVE_C, &host_volume);

    memset(handles, 0, sizeof handles);
    /* Handles 0-5 are reserved. Only the three the host gave us refer to
     * anything, the rest are allocated but have no file behind them. */
    for (i = 0; i < STD_HANDLES; i++)
        handles[i].flags = HANDLE_ALLOCATED;
    handles[0].of = open_stream(stdin);
    handles[1].of = open_stream(stdout);
    handles[2].of = open_stream(stderr);

    tos_env = te;
}

void gemdos_file_free()
{
}
