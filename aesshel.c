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
