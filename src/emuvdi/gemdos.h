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

/* Host replacement for EmuTOS's include/gemdos.h, which is trap instructions.
 *
 * The AES reaches GEMDOS to allocate memory and to read files. Memory it asks
 * for belongs to the AES rather than to the application, so it comes from the
 * host heap - see bdosbind.h and aeskernel.c. The file calls are not here yet:
 * the resource loader is the only thing that wants them, and where a resource
 * file is read from is a question for the kernel, which has to translate a TOS
 * path the way GEMDOS does.
 */

#ifndef GEMDOS_H
#define GEMDOS_H

/* DTA, which is where a directory walk puts what it found */
#include "bdosdefs.h"

void *dos_alloc_anyram(LONG nbytes);
WORD dos_free(void *maddr);

/*
 * Reading a resource file. The name is a TOS path, so opening it means
 * translating it the way GEMDOS does, which is the kernel's to do: it is the
 * kernel that knows which application asked and what its current drive and
 * directory are. Until then these report that the file is not there.
 */
LONG dos_open(char *pname, WORD access);
WORD dos_close(WORD handle);
LONG dos_read(WORD handle, LONG cnt, void *pbuffer);
LONG dos_lseek(WORD handle, WORD smode, LONG sofst);

/*
 * Walking a directory, which the file selector does and nothing else in the
 * AES does. These are GEMDOS's calls answered against the host filesystem -
 * see hostfs.c for why they are not tosemu's own.
 */
void dos_sdta(DTA *where);
DTA *dos_gdta(void);
LONG dos_sfirst(char *pattern, WORD attributes);
LONG dos_snext(void);
WORD dos_gdrv(void);
LONG dos_avail_anyram(void);

#endif /* GEMDOS_H */
