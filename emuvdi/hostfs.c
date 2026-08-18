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
 * Walking a directory, for the parts of the AES that read one.
 *
 * The file selector is the only thing that does, and it is the reason this
 * exists. Everything else reused from EmuTOS computes or draws, which is the
 * same work wherever it happens; the file selector asks what is on the disk,
 * and asking has to be answered by whoever owns the disk.
 *
 * That is this side. tosemu's GEMDOS is host C already, but every entrance to
 * it reads its arguments off the 68000's stack and keeps its answers in the
 * machine's memory, so it cannot be called from here. What can be is the part
 * underneath: turning a TOS path into a host one, which gemdosfile.c does, and
 * then the host's own directory reading.
 *
 * So this is GEMDOS's directory calls written a second time, against the host
 * rather than against the machine. That is a thing worth being uncomfortable
 * about, and the discomfort is bounded: it is four calls, they are the four
 * simplest, and what they share with the originals is the path translation
 * rather than a copy of it.
 */

#include "emutos.h"
#include "gemdos.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* tosemu's own, for turning a TOS path into a host one. The include path puts
 * emuvdi first, so this one is named the way it is reached from here. */
#include "../files.h"

/*
 * The last one of a character in a string.
 *
 * Written out because EmuTOS has a string.h of its own and it comes first on
 * the include path, which is what lets the rest of the port compile - and it
 * declares the handful of string functions EmuTOS uses, which does not include
 * this one.
 */
static const char *last_of(const char *s, char c)
{
    const char *found = 0;

    for (; *s; s++)
        if (*s == c)
            found = s;

    return found;
}

/* Where the answers go, which the caller says and then reads */
static DTA *dta;

/*
 * What is being walked. One at a time, which is what GEMDOS gives: the DTA is
 * the search as well as the answer, and a second Fsfirst before the first is
 * finished ends the first one.
 */
static struct {
    DIR *dir;
    char pattern[16];       /* The 8.3 the caller asked for, in capitals */
    char where[PATH_MAX+1]; /* The host directory it is reading */
    WORD attributes;        /* What kinds of thing to answer with */
} walk;

void dos_sdta(DTA *where)
{
    dta = where;
}

DTA *dos_gdta(void)
{
    return dta;
}

/*
 * The current drive, which is C. tosemu puts the TOS filesystem there because
 * A and B are the floppies and a machine that boots from a hard disk starts on
 * the hard disk.
 */
WORD dos_gdrv(void)
{
    return 2;
}

/*
 * How much memory is left. The AES asks so that it can decide whether to try
 * something, and there is no answer that means anything here - the host will
 * give it what it asks for or fail, and either way it finds out then. A large
 * number is the honest form of "do not decide anything on this".
 */
LONG dos_avail_anyram(void)
{
    return 8L * 1024 * 1024;
}

/*
 * Whether a name matches a pattern, TOS style: ? is any one character and * is
 * the rest of the name or the rest of the extension, separately, because a TOS
 * name is eight characters and three rather than a string with a dot in it.
 */
static int matches(const char *name, const char *pattern)
{
    int i;

    for (i = 0; i < 12; i++)
    {
        char p = pattern[i];
        char n = name[i];

        if (p == 0)
            return n == 0;

        if (p == '*')
        {
            /* The rest of this part of the name. Skip to the dot in both, or
             * to the end if there is no more pattern after it. */
            const char *pdot = strchr(pattern + i, '.');
            const char *ndot = strchr(name + i, '.');

            if (!pdot)
                return 1;

            if (!ndot)
                return pdot[1] == '*' || pdot[1] == 0;

            return matches(ndot, pdot);
        }

        if (n == 0)
            return 0;

        if (p != '?' && toupper((unsigned char)p) != toupper((unsigned char)n))
            return 0;
    }

    return 1;
}

/* A host name as TOS would show it: capitals, and no longer than eight and
 * three. Anything that does not fit is not a TOS name and is left out. */
static int tos_name(const char *from, char *to)
{
    const char *dot = last_of(from, '.');
    size_t stem = dot ? (size_t)(dot - from) : strlen(from);
    size_t ext = dot ? strlen(dot + 1) : 0;
    size_t i, n = 0;

    if (stem == 0 || stem > 8 || ext > 3)
        return 0;

    for (i = 0; i < stem; i++)
        to[n++] = (char)toupper((unsigned char)from[i]);

    if (ext)
    {
        to[n++] = '.';
        for (i = 0; i < ext; i++)
            to[n++] = (char)toupper((unsigned char)dot[1 + i]);
    }

    to[n] = 0;

    return 1;
}

/* Fills the DTA from one directory entry, or says it is not one to answer
 * with */
static int answer_with(const char *name)
{
    char host[PATH_MAX+1];
    char shown[16];
    struct stat about;
    struct tm *when;

    if (!tos_name(name, shown))
        return 0;

    if (!matches(shown, walk.pattern))
        return 0;

    snprintf(host, sizeof host, "%s/%s", walk.where, name);

    if (stat(host, &about) != 0)
        return 0;

    /*
     * Directories are answered with only when they were asked for, and files
     * always: that is what the attribute argument means to Fsfirst, and the
     * file selector uses it to list folders and files separately.
     */
    if (S_ISDIR(about.st_mode))
    {
        if (!(walk.attributes & FA_SUBDIR))
            return 0;

        dta->d_attrib = FA_SUBDIR;
        dta->d_length = 0;
    }
    else if (S_ISREG(about.st_mode))
    {
        dta->d_attrib = 0;
        dta->d_length = (LONG)about.st_size;
    }
    else
        return 0;       /* Not a thing TOS has */

    /* The time and date, packed the way TOS packs them: two seconds to the
     * tick, and years from 1980 */
    when = localtime(&about.st_mtime);
    if (when)
    {
        dta->d_time = (UWORD)((when->tm_hour << 11) | (when->tm_min << 5)
                              | (when->tm_sec / 2));
        dta->d_date = (UWORD)(((when->tm_year - 80) << 9)
                              | ((when->tm_mon + 1) << 5) | when->tm_mday);
    }
    else
        dta->d_time = dta->d_date = 0;

    strcpy(dta->d_fname, shown);

    return 1;
}

static void walk_done(void)
{
    if (walk.dir)
        closedir(walk.dir);

    memset(&walk, 0, sizeof walk);
}

/*
 * The first thing matching a pattern, and then the next one.
 *
 * The pattern is the last part of the path and everything before it is the
 * directory, which is the shape Fsfirst takes: "C:\GEM\*.RSC" is a directory
 * and a pattern rather than a name that happens to have stars in it.
 */
LONG dos_sfirst(char *pattern, WORD attributes)
{
    char path[PATH_MAX+1];
    char *last;

    if (!dta)
        return -33L;            /* EFILNF, there being nowhere to answer */

    walk_done();

    snprintf(path, sizeof path, "%s", pattern ? pattern : "");

    last = (char *)last_of(path, '\\');
    if (!last)
        last = (char *)last_of(path, ':');

    if (last)
    {
        snprintf(walk.pattern, sizeof walk.pattern, "%s", last + 1);
        last[1] = 0;            /* keep the separator, so the drive is kept */
    }
    else
    {
        snprintf(walk.pattern, sizeof walk.pattern, "%s", path);
        path[0] = 0;
    }

    if (tos_path_to_host(path, walk.where) != 0)
        return -34L;            /* EPTHNF */

    walk.attributes = attributes;
    walk.dir = opendir(walk.where);

    if (!walk.dir)
        return -34L;

    return dos_snext();
}

LONG dos_snext(void)
{
    struct dirent *entry;

    if (!walk.dir || !dta)
        return -49L;            /* ENMFIL, no more files */

    while ((entry = readdir(walk.dir)) != NULL)
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        if (answer_with(entry->d_name))
            return 0L;
    }

    walk_done();

    return -49L;
}
