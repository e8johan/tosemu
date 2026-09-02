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

#ifndef FILES_H
#define FILES_H

#include <stdint.h>

/* Naming a file the way an application does.
 *
 * This is the public face of the GEMDOS path handling, so that Pexec can find
 * the program it has been asked to run without reaching into GEMDOS
 * internals. Everything else about a file is private to GEMDOS, see
 * gemdosfile_p.h.
 */

/* Turns a path as an application spells it, drive letter, backslashes and all,
 * into one the host understands. The buffer takes PATH_MAX+1 bytes.
 *
 * Returns 0, or a negative GEMDOS error when there is no such drive.
 */
int32_t tos_path_to_host(const char *tos_path, char *host_path);

#endif /* FILES_H */
