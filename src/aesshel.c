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
 * The shell, which is how a program starts another program.
 *
 * On an ST the shell was the desktop, and shel_write was an application asking
 * it to run something. What the desktop did with the request depended on which
 * of them it was: one mode meant "stop me and run this instead", another meant
 * "run this as well as me". Here there is no desktop yet, and a process per
 * application means the second of those is what fork already does, so that is
 * the one that works.
 *
 * shel_read is the other half and is nothing to do with starting anything: it
 * is an application asking what it was itself started with, which it cannot
 * find out any other way once its basepage has been reused.
 */

#include "aes_p.h"

#include <stdio.h>
#include <string.h>

#include <limits.h>
#include <unistd.h>

#include "aesclient.h"
#include "aesproto.h"
#include "files.h"
#include "gemdos.h"
#include "tossystem.h"
#include "m68k.h"

/*
 * What shel_write is being asked for, http://toshyp.atari.org/en/008013.html
 *
 * Nought was "quit and run this", which the desktop did by terminating the
 * asking application first. One is "run this as well". The rest arrived with
 * later versions of the AES and are about accessories and shutting the machine
 * down.
 */
#define SHW_EXEC     (0)
#define SHW_LAUNCH   (1)

uint32_t AES_shel_write()
{
    int16_t what = aes_intin(0);
    int16_t graphic = aes_intin(1);
    uint32_t path = aes_addrin(0);
    uint32_t tail = aes_addrin(1);
    uint32_t err;

    FUNC_TRACE_ENTER_ARGS {
        char name[128];
        int i;

        for (i = 0; i < (int)sizeof name - 1; i++)
        {
            name[i] = (char)m68k_read_memory_8(path + i);
            if (name[i] == 0)
                break;
        }
        name[i] = 0;

        printf("    %d, %s, [%s]\n", what,
               graphic ? "a GEM program" : "a character program", name);
    }

    switch (what)
    {
        case SHW_EXEC:
        case SHW_LAUNCH:
            /*
             * Both start the program. They differ in what happens to the
             * application that asked - nought had it stop first - and stopping
             * it is not this call's to do: it returns, and the application
             * carries on to its own appl_exit. An application that meant nought
             * and is still running is one program too many rather than one too
             * few, which is the better way round to be wrong.
             */
            err = tos_start_program(path, tail);

            /*
             * A GEMDOS error comes back as a large negative number, and
             * shel_write answers nought for "could not" and one for "did".
             */
            if ((int32_t)err < 0)
            {
                FUNC_TRACE_ARGS {
                    printf("    could not start it: %d\n", (int32_t)err);
                }

                return AES_ERROR;
            }

            return AES_E_OK;

        default:
            halt_execution();
            printf("AES shel_write was asked for %d, which is not one of the "
                   "two it has here - see aesshel.c\n", what);
            return AES_ERROR;
    }
}

/*
 * shel_read - what this application was started with
 *
 * The path it was loaded from and the command line it was given. An application
 * reads its command line out of its basepage at startup, and this is how it
 * asks again later, once whatever was there has been overwritten.
 */
uint32_t AES_shel_read()
{
    uint32_t path = aes_addrin(0);
    uint32_t tail = aes_addrin(1);
    const char *name = tos_program_name();
    int i;

    FUNC_TRACE_ENTER

    if (!path || !tail)
        return AES_ERROR;

    /*
     * The name rather than the path it was loaded from, because that is what
     * this side of the emulator kept: a TOS path is worked out from a host one
     * when a program is started and not written down afterwards. An application
     * that wants to find its own files uses the application directory, which is
     * a different call.
     */
    for (i = 0; i < 8 && name[i] && name[i] != ' '; i++)
        m68k_write_memory_8(path + i, (uint8_t)name[i]);
    m68k_write_memory_8(path + i, 0);

    /* A command line is a length byte and then the characters, and this one is
     * empty: what the application was given went into its basepage, and it read
     * it from there */
    m68k_write_memory_8(tail, 0);
    m68k_write_memory_8(tail + 1, 0);

    return AES_E_OK;
}


/* Finding a file, and the rest of what the shell is asked ******************/

/* A TOS path is short, and so is everything else here */
#define MAX_PATH (128)

/* Where a program's basepage is, and where the environment's address sits
 * inside one */
#define TOS_BASEPAGE (0x800)

/* Copies a string out of the machine, and back into it */
static void string_in(uint32_t address, char *to, int size)
{
    int i;

    to[0] = 0;

    if (!address)
        return;

    for (i = 0; i < size - 1; i++)
    {
        to[i] = (char)m68k_read_memory_8(address + i);

        if (to[i] == 0)
            return;
    }

    to[size - 1] = 0;
}

static void string_out(uint32_t address, const char *from)
{
    int i;

    if (!address)
        return;

    for (i = 0; from[i]; i++)
        m68k_write_memory_8(address + i, (uint8_t)from[i]);

    m68k_write_memory_8(address + i, 0);
}

/* Whether a TOS path names something that is there */
static int it_is_there(const char *tos_path)
{
    char host[PATH_MAX + 1];

    if (tos_path_to_host(tos_path, host) != 0)
        return 0;

    return access(host, F_OK) == 0;
}

/*
 * shel_find - where a file is
 *
 * An application hands over a name and gets back a path to it, which is how a
 * GEM program finds its own resource file without knowing where it was started
 * from. GEM looked in the current directory, then in the directory the
 * application was loaded from, then along the path.
 *
 * The first two are what matter and are what is done. The third was for
 * finding other people's programs and is the part that needs a shell to have
 * set a path in the first place, which nothing here does.
 */
uint32_t AES_shel_find()
{
    uint32_t buffer = aes_addrin(0);
    char name[MAX_PATH], tried[MAX_PATH];
    const char *program;
    const char *slash;

    string_in(buffer, name, sizeof name);

    FUNC_TRACE_ENTER_ARGS {
        printf("    looking for [%s]\n", name);
    }

    if (!name[0])
        return AES_ERROR;

    /* Where it says, which covers a name with a path already on it as well as
     * one in the current directory */
    if (it_is_there(name))
        return AES_E_OK;

    /*
     * Beside the program, which is where an application keeps its resource. The
     * name of the program is what this side knows; the directory it came from
     * is in front of it.
     */
    program = tos_program_name();
    slash = 0;
    if (program)
    {
        const char *walk;

        for (walk = program; *walk; walk++)
            if (*walk == '\\' || *walk == '/' || *walk == ':')
                slash = walk;
    }

    if (slash)
    {
        int n = (int)(slash - program) + 1;

        if (n > (int)sizeof tried - 1)
            n = (int)sizeof tried - 1;

        memcpy(tried, program, n);
        tried[n] = 0;
        snprintf(tried + n, sizeof tried - n, "%s", name);

        if (it_is_there(tried))
        {
            string_out(buffer, tried);

            FUNC_TRACE_ARGS {
                printf("    found beside the program: [%s]\n", tried);
            }

            return AES_E_OK;
        }
    }

    FUNC_TRACE_ARGS {
        printf("    not found\n");
    }

    return AES_ERROR;
}

/*
 * shel_envrn - what an environment variable says
 *
 * The application hands over a name to look for, with its equals sign, and
 * gets back a pointer to what follows it inside its own environment. A pointer
 * into the environment rather than a copy, because that is what GEM did and
 * an application is entitled to keep it.
 */
uint32_t AES_shel_envrn()
{
    uint32_t answer_at = aes_addrin(0);
    uint32_t name_at = aes_addrin(1);
    char name[MAX_PATH];
    /*
     * The environment, which is in the basepage: a program is given one when
     * it is started and the basepage is where its address is kept. Read from
     * there rather than from anything this side remembers, because the
     * application is entitled to have changed it.
     */
    uint32_t environment = m68k_read_memory_32(TOS_BASEPAGE + 0x2c);
    uint32_t walk;
    int len;

    string_in(name_at, name, sizeof name);

    FUNC_TRACE_ENTER_ARGS {
        printf("    looking for [%s]\n", name);
    }

    if (!answer_at || !name[0] || !environment)
        return AES_ERROR;

    len = (int)strlen(name);

    /*
     * The environment is strings one after another, ending with an empty one.
     * Each is compared from the front, so the name has to carry its own equals
     * sign - which is the caller's business and is how GEM defined it.
     */
    for (walk = environment; m68k_read_memory_8(walk); )
    {
        int i;

        for (i = 0; i < len; i++)
            if ((char)m68k_read_memory_8(walk + i) != name[i])
                break;

        if (i == len)
        {
            m68k_write_memory_32(answer_at, walk + len);

            FUNC_TRACE_ARGS {
                printf("    found it\n");
            }

            return AES_E_OK;
        }

        while (m68k_read_memory_8(walk))
            walk++;
        walk++;
    }

    /* Not there, which is answered with nothing rather than with an error: an
     * application asks about variables that may not be set */
    m68k_write_memory_32(answer_at, 0);

    return AES_E_OK;
}

/*
 * shel_get and shel_put - the desktop's own notes
 *
 * A buffer the desktop keeps, holding what an ST wrote into DESKTOP.INF: which
 * windows were open, what the icons were called, which resolution to start in.
 * The AES neither reads it nor writes it - it holds it - so this is a place to
 * put something rather than a setting.
 *
 * It belongs to the session rather than to an application, which makes it the
 * daemon's, for the same reason the scrap is: one application writes it and
 * another expects to read what was written.
 */
uint32_t AES_shel_get()
{
    uint32_t buffer = aes_addrin(0);
    int16_t length = aes_intin(0);
    char notes[AESD_NOTES];
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    up to %d bytes\n", length);
    }

    if (!buffer || length <= 0)
        return AES_ERROR;

    aes_client_notes_get(notes, sizeof notes);

    for (i = 0; i < length && i < (int)sizeof notes; i++)
        m68k_write_memory_8(buffer + i, (uint8_t)notes[i]);

    return AES_E_OK;
}

uint32_t AES_shel_put()
{
    uint32_t buffer = aes_addrin(0);
    int16_t length = aes_intin(0);
    char notes[AESD_NOTES];
    int i;

    FUNC_TRACE_ENTER_ARGS {
        printf("    %d bytes\n", length);
    }

    if (!buffer || length <= 0)
        return AES_ERROR;

    for (i = 0; i < length && i < (int)sizeof notes - 1; i++)
        notes[i] = (char)m68k_read_memory_8(buffer + i);
    notes[i] = 0;

    aes_client_notes_set(notes, i);

    return AES_E_OK;
}

/*
 * shel_rdef and shel_wdef - which program the desktop runs when nothing else is
 *
 * On an ST this was how a replacement desktop said it was the shell now. There
 * is no shell here to replace, so what is remembered is remembered and given
 * back, and nothing acts on it. An application that sets it and reads it gets
 * what it set, which is all it can check.
 */
static char shell_command[MAX_PATH] = "";
static char shell_directory[MAX_PATH] = "";

uint32_t AES_shel_rdef()
{
    FUNC_TRACE_ENTER

    string_out(aes_addrin(0), shell_command);
    string_out(aes_addrin(1), shell_directory);

    return AES_E_OK;
}

uint32_t AES_shel_wdef()
{
    FUNC_TRACE_ENTER

    string_in(aes_addrin(0), shell_command, sizeof shell_command);
    string_in(aes_addrin(1), shell_directory, sizeof shell_directory);

    FUNC_TRACE_ARGS {
        printf("    [%s] in [%s]\n", shell_command, shell_directory);
    }

    return AES_E_OK;
}
