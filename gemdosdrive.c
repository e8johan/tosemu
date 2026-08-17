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

#include "gemdosdrive_p.h"

#include <ctype.h>
#include <stddef.h>

/* The drive table.
 *
 * TOS addresses drives by number, 0 is A: and 2 is C:. tosemu presents a
 * single drive, C:, backed by the host file system. The table exists so that
 * the drive a path refers to is decided in one place rather than assumed by
 * every file function.
 */
static struct volume *drives[DRIVE_COUNT];
static int current_drive = DRIVE_C;

void drive_register(int drive, struct volume *volume)
{
    if (drive < 0 || drive >= DRIVE_COUNT)
        return;

    drives[drive] = volume;
}

struct volume *drive_volume(int drive)
{
    if (drive < 0 || drive >= DRIVE_COUNT)
        return NULL;

    return drives[drive];
}

uint32_t drive_map(void)
{
    uint32_t map = 0;
    int i;

    for (i = 0; i < DRIVE_COUNT; ++i)
        if (drives[i])
            map |= 1u << i;

    return map;
}

int drive_mediach(int drive)
{
    struct volume *v = drive_volume(drive);

    if (!v || !v->mediach)
        return 0; /* A drive that is not there cannot have changed */

    return v->mediach();
}

int drive_current(void)
{
    return current_drive;
}

int drive_set_current(int drive)
{
    if (!drive_volume(drive))
        return -1;

    current_drive = drive;

    return 0;
}

int drive_from_path(const char *tos_path, const char **rest)
{
    int drive = current_drive;

    /* A path may name its drive, as in C:\FOLDER\FILE.TXT */
    if (tos_path[0] && tos_path[1] == ':')
    {
        if (!isalpha((unsigned char)tos_path[0]))
            return -1;

        drive = toupper((unsigned char)tos_path[0]) - 'A';
        tos_path += 2;
    }

    if (rest)
        *rest = tos_path;

    if (!drive_volume(drive))
        return -1;

    return drive;
}
