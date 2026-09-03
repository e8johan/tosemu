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
 * The file selector, which is how a GEM application opens a file.
 *
 * It is one call and it does everything: puts up a dialog, lists a directory,
 * lets the person walk into folders and change drives, and answers with a path
 * and a name. An application that has one has an Open menu; an application
 * without one has almost nothing.
 *
 * The dialog itself is EmuTOS's, out of the AES's own resource, and so is the
 * walking about. What had to be written is underneath it - reading a directory
 * from the host rather than from a machine that has no disks, which is
 * emuvdi/hostfs.c.
 *
 * The two calls differ by one thing: the later one lets the application put
 * its own words at the top, so that a program asking where to save something
 * can say so rather than showing the same box it showed for opening.
 */

#include "aes_p.h"

#include <stdio.h>
#include <string.h>

#include "emuvdi/emuvdi.h"
#include "gem_p.h"
#include "m68k.h"

/* A TOS path and a TOS name, which are both short */
#define MAX_PATH  (128)
#define MAX_NAME  (16)
#define MAX_LABEL (32)

/* Copies a string out of the machine */
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

/* And back into it, which is how the answer is given: the application handed
 * over the buffers and expects to read them afterwards */
static void string_out(uint32_t address, const char *from)
{
    int i;

    if (!address)
        return;

    for (i = 0; from[i]; i++)
        m68k_write_memory_8(address + i, (uint8_t)from[i]);

    m68k_write_memory_8(address + i, 0);
}

static uint32_t file_selector(int with_label)
{
    uint32_t path_at = aes_addrin(0);
    uint32_t name_at = aes_addrin(1);
    uint32_t label_at = with_label ? aes_addrin(2) : 0;
    char path[MAX_PATH], name[MAX_NAME], label[MAX_LABEL];
    int16_t button = 0;
    int16_t answer;

    string_in(path_at, path, sizeof path);
    string_in(name_at, name, sizeof name);
    string_in(label_at, label, sizeof label);

    FUNC_TRACE_ENTER_ARGS {
        printf("    path [%s], name [%s]", path, name);
        if (with_label)
            printf(", titled [%s]", label);
        printf("\n");
    }

    if (!gem_start())
        return AES_ERROR;

    /*
     * A path with nothing in it is an application that has not been anywhere
     * yet, and GEM answers that by showing the current drive. Saying so here
     * rather than leaving it empty is what stops the first thing it lists
     * being nothing at all.
     */
    if (!path[0])
        snprintf(path, sizeof path, "C:\\*.*");

    /* The dialog reserves the screen it sits on, the same way form_dial does
     * for an application's own dialogs, so that it is a window of the
     * desktop's rather than a picture drawn on nothing */
    answer = emuvdi_fsel_input(path, name, &button,
                               with_label && label[0] ? label : 0);

    string_out(path_at, path);
    string_out(name_at, name);

    aes_set_intout(1, button);

    FUNC_TRACE_ARGS {
        printf("    %s, path [%s], name [%s]\n",
               button ? "chosen" : "cancelled", path, name);
    }

    gem_present();

    return (uint32_t)(uint16_t)answer;
}

uint32_t AES_fsel_input()
{
    return file_selector(0);
}

uint32_t AES_fsel_exinput()
{
    return file_selector(1);
}
