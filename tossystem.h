/*
 * TOSEMU - an emulated environment for TOS applications
 * Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
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

#ifndef TOSSYSTEM_H
#define TOSSYSTEM_H

#include <stdint.h>

struct basepage;

struct tos_environment {
    uint64_t size;
    void *appmem;
    void *supermem;
    void *staticmem0;
    void *staticmem1;
    void *biosram;

    uint32_t tsize, 
             dsize, 
             bsize, 
             ssize;

    struct basepage *bp;

    char *base_path;
};

/* The longest command line text a basepage can hold. Its field is 128 bytes,
 * of which a length byte and a terminating zero are not text. */
#define TOS_CMDLIN_MAX (126)

/* Maps a TOS binary, checking that it is one.
 *
 * Returns its contents, to be released with unmap_tos_binary, or NULL after
 * reporting why not.
 */
void *map_tos_binary(const char *path, uint64_t *size);
void unmap_tos_binary(void *binary, uint64_t size);

/* The command line and the environment tosemu itself was started with, in the
 * form an application is handed them. host_cmdlin returns the length of the
 * text it wrote, which needs room for TOS_CMDLIN_MAX characters.
 * host_environment returns a block the caller frees.
 */
int host_cmdlin(char *buf, int argc, char **argv);
char *host_environment(uint32_t *len);

int init_tos_environment(struct tos_environment *te, void *binary,
                         uint64_t binary_size,
                         const char *cmdlin, int cmdlin_len,
                         const char *env, uint32_t env_len);
void free_tos_environment(struct tos_environment *te);

/* Runs the application until it terminates, or until something it asked for
 * turns out not to be implemented */
void run_tos_environment(struct tos_environment *te);

/* Reserve a block of ST RAM that lives for as long as the application does.
 *
 * Several BIOS and XBIOS calls hand back a pointer to a structure the system
 * owns, a screen buffer or a vector table for instance. They come from an area
 * outside the TPA, so that reserving one does not take memory away from the
 * application. Returns an address in the emulated machine, or 0 when the area
 * is exhausted. There is no matching free, the whole area goes at once.
 */
uint32_t bios_static_alloc(uint32_t len);

void halt_execution();

#endif /* TOSSYSTEM_H */
