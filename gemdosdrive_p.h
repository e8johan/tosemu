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

#ifndef GEMDOSDRIVE_P_H
#define GEMDOSDRIVE_P_H

#include <stdint.h>

#include "drives.h"

#define DRIVE_A     (0)
#define DRIVE_C     (2)
#define DRIVE_COUNT (26)

/* What a drive is backed by.
 *
 * Only the host file system exists today, and it is registered for C: by
 * gemdosfile. A second implementation is what it takes to make another drive
 * work, for instance a floppy image mounted as A:, and it is the reason the
 * drive table is expressed as operations rather than as a bare flag.
 */
struct volume {
    const char *name;

    /* Turn a TOS path that has had its drive prefix removed into something
     * this volume can act upon. The host volume produces a host path.
     * Returns 0 on success, or a negative GEMDOS error. */
    int32_t (*resolve)(const char *tos_path, char *out);

    /* Media change state, using the BIOS Mediach encoding, see drives.h */
    int (*mediach)(void);
};

/* Attach a volume to a drive, or detach it by passing NULL */
void drive_register(int drive, struct volume *volume);

/* The volume backing a drive, NULL when the drive does not exist */
struct volume *drive_volume(int drive);

/* The drive that a path without an explicit prefix refers to */
int drive_current(void);
int drive_set_current(int drive);

/* Split an optional "X:" prefix off a TOS path.
 *
 * Stores the remainder of the path, i.e. the part without the prefix, in rest.
 * Returns the drive the path refers to, which is the current drive when there
 * is no prefix, or -1 when the prefix names a drive that does not exist.
 */
int drive_from_path(const char *tos_path, const char **rest);

#endif /* GEMDOSDRIVE_P_H */
