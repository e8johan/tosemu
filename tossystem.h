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

/* The command line field of a basepage, and the longest text it can hold: the
 * field also carries a length byte and a terminating zero.
 *
 * A length byte of 127 is not a length. It is the mark saying the arguments
 * did not fit and were passed through the ARGV variable in the environment
 * instead, so a command line is carried from one basepage to another as it
 * stands rather than measured and rebuilt.
 */
#define TOS_CMDLIN_SIZE (128)
#define TOS_CMDLIN_MAX  (TOS_CMDLIN_SIZE - 2)

/* Maps a TOS binary, checking that it is one.
 *
 * Returns its contents, to be released with unmap_tos_binary, or NULL after
 * reporting why not.
 */
void *map_tos_binary(const char *path, uint64_t *size);
void unmap_tos_binary(void *binary, uint64_t size);

/* The command line and the environment tosemu itself was started with, in the
 * form an application is handed them. host_cmdlin fills in a whole command
 * line field, so its buffer takes TOS_CMDLIN_SIZE bytes. host_environment
 * returns a block the caller frees.
 */
void host_cmdlin(char *field, int argc, char **argv);
char *host_environment(uint32_t *len);

int init_tos_environment(struct tos_environment *te, void *binary,
                         uint64_t binary_size,
                         const char *cmdlin,
                         const char *env, uint32_t env_len);
void free_tos_environment(struct tos_environment *te);

/* Copies an environment block out of the emulated memory, so that it survives
 * the machine being rebuilt around another application. Returns a block the
 * caller frees, in the form init_tos_environment expects.
 */
char *tos_environment(uint32_t addr, uint32_t *len);

/* A basepage and the program behind it start 0x100 apart */
#define TOS_BASEPAGE_SIZE (0x100)

/*
 * Builds a basepage at base in the emulated memory, for a program owning the
 * block that runs from base to base+len, and loads binary into it. The
 * program itself goes 0x100 above the basepage.
 *
 * A null binary makes a basepage with no program behind it, which is what
 * Pexec is asked for when it is only making room for one.
 *
 * Returns TOS_LOAD_OK, or why not. GEMDOS error codes belong to GEMDOS, so
 * the caller is the one to turn these into them.
 */
#define TOS_LOAD_OK        (0)
#define TOS_LOAD_BADFORMAT (-1)
#define TOS_LOAD_NOROOM    (-2)

int32_t place_program(uint32_t base, uint32_t len, const void *binary,
                      uint64_t size, const char *cmdlin, uint32_t env,
                      uint32_t parent);

/*
 * Hands the loop a program that is already in memory, which is what Pexec is
 * asked for once another program has loaded one and set its basepage up.
 */
void exec_tos_basepage(uint32_t basepage);

/*
 * Hands the loop an application to run in place of the one running now, which
 * is how Pexec starts a program. It cannot be started from where Pexec is
 * called: that is inside a trap, and inside Musashi, neither of which survives
 * the CPU being reset under them.
 *
 * The binary is mapped here rather than when the swap happens, so that a file
 * that is not a program is reported to whoever called Pexec instead of being
 * discovered once there is nowhere left to report it. Takes ownership of env.
 *
 * Returns 0, or -1 when the binary could not be mapped.
 */
int exec_tos_binary(const char *host_path, const char *cmdlin,
                    char *env, uint32_t env_len);

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
