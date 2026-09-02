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
 * The scrap, which is what GEM calls the clipboard.
 *
 * It is a directory, and that is the whole of it. An application that cuts
 * something out writes it into a file called SCRAP with an extension saying
 * what kind of thing it is - .TXT for text, .IMG for a picture - and one that
 * pastes looks for the kinds it can read and takes the first it finds. Nothing
 * is held in memory and nothing is passed between programs directly.
 *
 * So the only thing two applications have to agree about is which directory,
 * and these two calls are how they agree. Everything else is GEMDOS writing
 * files, which works already.
 *
 * That makes this the smallest thing the daemon holds, and the clearest example
 * of what belongs there: not the data, which is a file, but the one fact both
 * ends must have the same answer for.
 */

#include "aes_p.h"

#include <stdio.h>
#include <string.h>

#include "aesclient.h"
#include "m68k.h"

/* A TOS path, which is short: a drive, directories of eight and three, and a
 * separator between each */
#define MAX_PATH (128)

uint32_t AES_scrp_read()
{
    uint32_t buffer = aes_addrin(0);
    char path[MAX_PATH];
    int i;

    FUNC_TRACE_ENTER

    if (!buffer)
        return AES_ERROR;

    aes_client_scrap_get(path, sizeof path);

    FUNC_TRACE_ARGS {
        printf("    scrap: [%s]\n", path);
    }

    /*
     * Nothing written yet, which is not an error to fail over: an application
     * that asks before anything has been cut is told there is nothing, and is
     * expected to grey its Paste out rather than to stop.
     */
    if (!path[0])
        return AES_ERROR;

    for (i = 0; i < MAX_PATH; i++)
    {
        m68k_write_memory_8(buffer + i, (uint8_t)path[i]);

        if (path[i] == 0)
            break;
    }

    return AES_E_OK;
}

uint32_t AES_scrp_write()
{
    uint32_t buffer = aes_addrin(0);
    char path[MAX_PATH];
    int i;

    for (i = 0; i < MAX_PATH - 1; i++)
    {
        path[i] = (char)m68k_read_memory_8(buffer + i);

        if (path[i] == 0)
            break;
    }
    path[i] = 0;

    FUNC_TRACE_ENTER_ARGS {
        printf("    scrap: [%s]\n", path);
    }

    aes_client_scrap_set(path);

    return AES_E_OK;
}
