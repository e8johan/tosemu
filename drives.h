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

#ifndef DRIVES_H
#define DRIVES_H

#include <stdint.h>

/* The drives tosemu presents to the application.
 *
 * This is the public face of the GEMDOS drive table, so that BIOS can answer
 * Drvmap and Mediach without reaching into GEMDOS internals. Everything else
 * about a drive is private to GEMDOS, see gemdosdrive_p.h.
 */

/* Bitmap of the drives that exist, bit 0 is A: and bit 25 is Z: */
uint32_t drive_map(void);

/* Whether the media in a drive may have been swapped, using the encoding of
 * the BIOS Mediach call:
 *
 *   0  the media has not changed
 *   1  the media may have changed
 *   2  the media has definitely changed
 *
 * A drive that does not exist reports 0.
 */
int drive_mediach(int drive);

#endif /* DRIVES_H */
