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

    /* How much of the machine the application owns, counted from its basepage
     * at 0x800.
     *
     * All of it for a program, because that is what TOS hands one and what
     * Mshrink is there to give back. An accessory owns room for itself and no
     * more: the AES sized the block when it loaded it, and an accessory that
     * Mallocs a stack for itself without Mshrinking first - which is what one
     * does, having nothing to give back - needs something left to Malloc from.
     */
    uint32_t tpa_len;

    /* Where its stack starts, which is the top of the block it owns - except
     * for an accessory, whose block is only as large as itself and has no room
     * for one. The AES ran an accessory on a stack of the AES's own until it
     * set up one of its own, which every accessory does in its first few
     * instructions, and this is that stack: outside the TPA, so that Malloc
     * can hand out everything above the accessory without handing out what it
     * is standing on. */
    uint32_t stack;

    void *appmem;
    void *supermem;
    void *staticmem0;
    void *staticmem1;
    void *biosram;

    /* The screen, which is the machine's rather than the program's and sits
     * above the TPA - see where it is reserved in tossystem.c */
    void *screenmem;

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
/* What this application is called: the program's name, in capitals, eight
 * characters padded with spaces, which is the form GEM uses */
const char *tos_program_name(void);

/*
 * Says that the application about to be built is an accessory, which decides
 * how much of the machine it is given rather than anything about what it does.
 *
 * Only the application tosemu was started with can be one: on an ST it is the
 * AES that loads accessories, and tosemu standing in for it is what running
 * one directly amounts to. A program that Pexecs an .ACC gets a program, the
 * same as TOS would give it.
 */
void tos_load_as_accessory(int yes);

int exec_tos_binary(const char *host_path, const char *cmdlin,
                    char *env, uint32_t env_len);

/* Runs the application until it terminates, or until something it asked for
 * turns out not to be implemented */
void run_tos_environment(struct tos_environment *te);

/* Reserve a block of ST RAM that lives for as long as the application does.
 *
 * Several BIOS and XBIOS calls hand back a pointer to a structure the system
 * owns, a vector table or a device's buffer for instance. They come from an
 * area outside the TPA, so that reserving one does not take memory away from
 * the application. Returns an address in the emulated machine, or 0 when the
 * area is exhausted. There is no matching free, the whole area goes at once.
 */
uint32_t bios_static_alloc(uint32_t len);

/*
 * Where the screen is in the machine, and how many bytes of it there are.
 *
 * This is what Physbase and Logbase answer with. Nothing is shown there - what
 * reaches a display is what the VDI drew on a surface of the host's - but an
 * application that draws without the VDI has to be given somewhere to draw,
 * and as much of it as it has been told the screen holds. It is reserved off
 * the top of the machine's RAM, which is where the machine kept its own; see
 * the note where that is done.
 */
uint32_t tos_screen_base(void);
uint32_t tos_screen_size(void);

void halt_execution();

/* Whether that has happened. The loop that runs the machine reads it for
 * itself; this is for the one place that runs the machine from inside a call -
 * host_userdef_draw in aestree.c - and has to stop when the loop would. */
int execution_halted();

#endif /* TOSSYSTEM_H */
