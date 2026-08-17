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
#include "tossystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "memory.h"
#include "utils.h"
#include "cpu.h"
#include "gemdos.h"
#include "xbios.h"
#include "bios.h"
#include "gem.h"

#include "m68k.h"

/* Basepage, as defined here:
 * http://www.yardley.cc/atari/compendium/atari-compendium-chapter-2-GEMDOS.htm#gdprocess
 * 
 * bigendian
 */
#pragma pack(push,2)
struct basepage {
    uint32_t p_lowtpa;
    uint32_t p_hitpa;
    uint32_t p_tbase, p_tlen;
    uint32_t p_dbase, p_dlen;
    uint32_t p_bbase, p_blen;
    uint32_t p_dta;
    uint32_t p_parent;
    uint32_t p_reserved;
    uint32_t p_env;
    uint8_t p_undef[80];
    uint8_t p_cmdlin[128];
};
#pragma pack(pop)

/* Header of executable, as defined here: 
 * http://www.yardley.cc/atari/compendium/atari-compendium-chapter-2-GEMDOS.htm#gdprocess 
 * 
 * big endian
 */
#pragma pack(push,2)
struct exec_header {
    uint16_t magic;
    uint32_t tsize, 
             dsize, 
             bsize, 
             ssize;
    uint32_t res;
    uint32_t flags;
    uint16_t absflag;
};
#pragma pack(pop)

#define SUPERMEMSIZE (512)

/* RAM for structures the system owns rather than the application, see
 * bios_static_alloc. It sits in the cartridge ROM range of the memory map
 * below, which no ST ever has RAM in and which is clear of the TPA, so that
 * reserving a screen buffer does not shrink the memory the application gets. */
#define BIOSRAMBASE (0xFA0000)
#define BIOSRAMSIZE (0x10000)

static uint32_t biosram_free;

int keepongoing;

void *map_tos_binary(const char *path, uint64_t *size)
{
    struct stat sb;
    void *data;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd == -1)
    {
        printf("Error: failed to open '%s'\n", path);
        return NULL;
    }

    if (fstat(fd, &sb) != 0)
    {
        printf("Error: failed to stat '%s'\n", path);
        close(fd);
        return NULL;
    }

    data = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);

    if (data == MAP_FAILED)
    {
        printf("Error: failed to mmap '%s'\n", path);
        return NULL;
    }

    /* Every TOS executable starts with the magic 0x601a */
    if (sb.st_size < 2 ||
        ((uint8_t *)data)[0] != 0x60 || ((uint8_t *)data)[1] != 0x1a)
    {
        printf("Error: invalid magic in '%s'\n", path);
        munmap(data, sb.st_size);
        return NULL;
    }

    *size = sb.st_size;

    return data;
}

void unmap_tos_binary(void *binary, uint64_t size)
{
    munmap(binary, size);
}

uint32_t bios_static_alloc(uint32_t len)
{
    uint32_t address = biosram_free;

    /* Keep every block even, a structure handed to a 68000 may be read as a
     * word or a long */
    len = (len + 1) & ~1u;

    if (len > BIOSRAMSIZE || address - BIOSRAMBASE > BIOSRAMSIZE - len)
        return 0;

    biosram_free += len;

    return address;
}

/*
 * Applies a program's relocation table, which lists the places in it holding
 * an address as the gaps between them.
 *
 * A binary is written as if it had been loaded at zero, so every one of those
 * places has the address it really landed at added to it.
 */
static void relocate_program(uint32_t tbase, const void *binary)
{
    const struct exec_header *header = binary;
    const uint8_t *ptr;
    uint32_t adr;

    if (header->absflag)
        return;

    ptr = (const uint8_t *)binary + sizeof(struct exec_header)
        + endianize_32(header->tsize)
        + endianize_32(header->dsize)
        + endianize_32(header->ssize);

    /* The table opens with the first address, and carries on with the gap to
     * each of the ones after it */
    adr = tbase + endianize_32(*(const uint32_t *)ptr);
    ptr += 4;

    if (adr == tbase)
        return; /* A first offset of zero means there is nothing to relocate */

    m68k_write_memory_32(adr, m68k_read_memory_32(adr) + tbase);

    while (*ptr)
    {
        /* A gap of one is the mark for a jump of 254, which is how a gap too
         * wide for a byte is written */
        if (*ptr == 1)
            adr += 0xfe;
        else
        {
            adr += *ptr;
            m68k_write_memory_32(adr, m68k_read_memory_32(adr) + tbase);
        }

        ptr++;
    }
}

/* Copies a structure the emulator built into the memory of the machine */
static void write_bytes(uint32_t addr, const void *src, uint32_t len)
{
    const uint8_t *from = src;
    uint32_t i;

    for (i = 0; i < len; i++)
        m68k_write_memory_8(addr + i, from[i]);
}

int32_t place_program(uint32_t base, uint32_t len, const void *binary,
                      uint64_t size, const char *cmdlin, uint32_t env,
                      uint32_t parent)
{
    const struct exec_header *header = binary;
    struct basepage bp;
    uint32_t tsize = 0, dsize = 0, bsize = 0;

    if (binary)
    {
        if (size < sizeof(struct exec_header))
            return TOS_LOAD_BADFORMAT;

        tsize = endianize_32(header->tsize);
        dsize = endianize_32(header->dsize);
        bsize = endianize_32(header->bsize);

        if (size < sizeof(struct exec_header) + tsize + dsize)
            return TOS_LOAD_BADFORMAT;
    }

    /* The basepage, the program and the memory it starts out with all have to
     * fit in the block that was set aside for them */
    if (len < TOS_BASEPAGE_SIZE ||
        len - TOS_BASEPAGE_SIZE < tsize + dsize + bsize)
        return TOS_LOAD_NOROOM;

    memset(&bp, 0, sizeof bp);
    bp.p_lowtpa = endianize_32(base);
    bp.p_hitpa = endianize_32(base + len);
    bp.p_tbase = endianize_32(base + TOS_BASEPAGE_SIZE);
    bp.p_tlen = endianize_32(tsize);
    bp.p_dbase = endianize_32(base + TOS_BASEPAGE_SIZE + tsize);
    bp.p_dlen = endianize_32(dsize);
    bp.p_bbase = endianize_32(base + TOS_BASEPAGE_SIZE + tsize + dsize);
    bp.p_blen = endianize_32(bsize);
    bp.p_parent = endianize_32(parent);
    bp.p_env = endianize_32(env);
    /* TOS defaults the Disk Transfer Address to the command line */
    bp.p_dta = endianize_32(base + offsetof(struct basepage, p_cmdlin));
    memcpy(bp.p_cmdlin, cmdlin, TOS_CMDLIN_SIZE);

    write_bytes(base, &bp, sizeof bp);

    if (binary)
    {
        write_bytes(base + TOS_BASEPAGE_SIZE,
                    (const uint8_t *)binary + sizeof(struct exec_header),
                    tsize + dsize);

        /* The BSS is zeroed by TOS when loading a program */
        {
            uint32_t i, bss = base + TOS_BASEPAGE_SIZE + tsize + dsize;

            for (i = 0; i < bsize; i++)
                m68k_write_memory_8(bss + i, 0);
        }

        relocate_program(base + TOS_BASEPAGE_SIZE, binary);
    }

    return TOS_LOAD_OK;
}

/*
 * Writes an environment block into system RAM and returns its address.
 *
 * TOS stores the environment as a run of zero terminated NAME=value strings
 * ended by an empty one. It belongs to the parent process rather than to the
 * application, which is why it goes in system RAM rather than in the TPA.
 */
static uint32_t place_environment(const char *block, uint32_t len)
{
    uint32_t base;
    uint32_t i;

    base = bios_static_alloc(len);
    if (base == 0)
    {
        /* More than the system has room for. An application still needs an
         * environment to look in, so hand it an empty one. */
        base = bios_static_alloc(1);
        if (base == 0)
            return 0;

        m68k_write_memory_8(base, 0);
        return base;
    }

    for (i = 0; i < len; i++)
        m68k_write_memory_8(base + i, block[i]);

    return base;
}

/*
 * Builds an environment block out of the one tosemu was started with, so that
 * a variable an application looks for can be set from the host shell - Lattice
 * C finds its header files through INCLUDE.
 */
char *host_environment(uint32_t *len)
{
    extern char **environ;
    char *block, *dest;
    uint32_t n = 1; /* The empty string ending the block */
    int i;

    for (i = 0; environ[i]; i++)
        n += strlen(environ[i]) + 1;

    block = malloc(n);
    if (block == NULL)
        return NULL;

    dest = block;
    for (i = 0; environ[i]; i++)
    {
        strcpy(dest, environ[i]);
        dest += strlen(environ[i]) + 1;
    }
    *dest = 0;

    *len = n;

    return block;
}

/* Far more than a TOS environment ever holds, and small enough next to the
 * system RAM that placing one still leaves room for a screen buffer. An
 * application pointing Pexec at something that is not an environment is
 * stopped here rather than walked after through the whole address space. */
#define TOS_ENV_MAX (16*1024)

char *tos_environment(uint32_t addr, uint32_t *len)
{
    char *block;
    uint32_t n = 0;
    uint32_t i;

    /* Walk the strings until an empty one, which is what ends the block */
    while (n < TOS_ENV_MAX && m68k_read_disassembler_8(addr + n) != 0)
    {
        while (n < TOS_ENV_MAX && m68k_read_disassembler_8(addr + n) != 0)
            n++;
        n++; /* The zero ending this string */
    }
    n++; /* The empty string ending the block */

    if (n > TOS_ENV_MAX)
        n = TOS_ENV_MAX;

    block = malloc(n);
    if (block == NULL)
        return NULL;

    for (i = 0; i < n; i++)
        block[i] = m68k_read_disassembler_8(addr + i);

    /* However odd what we were pointed at, what we pass on ends the way an
     * environment has to */
    block[n-1] = 0;

    *len = n;

    return block;
}

/* The command line of the application tosemu was asked to start, which is the
 * arguments after the binary with a space between them */
void host_cmdlin(char *field, int argc, char **argv)
{
    int i, n = 0;

    memset(field, 0, TOS_CMDLIN_SIZE);

    for (i = 0; i < argc; i++)
    {
        int len = strlen(argv[i]);
        int sep = (n != 0); /* Every argument but the first needs a space */

        /* An argument that does not fit is dropped, and so are the ones after
         * it, rather than leaving a half of one on the command line */
        if (n + sep + len > TOS_CMDLIN_MAX)
            break;

        if (sep)
            field[1 + n++] = ' ';

        memcpy(field + 1 + n, argv[i], len);
        n += len;
    }

    field[0] = n;
    field[1 + n] = 0;
}

/* Builds the emulated machine around a binary, short of the subsystem setup,
 * which differs between the first application and one replacing another */
static int load_tos_environment(struct tos_environment *te, void *binary,
                                uint64_t size,
                                const char *cmdlin,
                                const char *env, uint32_t env_len)
{
    struct exec_header *header;
    char *path;
    
    /* Ensure that binary is large enough to hold a header */
    if (size < sizeof(struct exec_header))
    {
        printf("Error: Too small binary\n");
        return -1;
    }
    
    /* Setup "static" data areas.
     *
     * Every area of the emulated machine starts out zeroed. A machine that is
     * built a second time, which is what Pexec does in the process it forked,
     * would otherwise be handed whatever the host heap still had in it, and
     * an application would read what the one before it left behind. */
    te->staticmem0 = calloc(1, 0x200);       /* 0x0 - 0x1ff */
    te->staticmem1 = calloc(1, 0x600-0x380); /* 0x380 - 0x5ff */

    /* Create supervisor memory for a stack */
    te->supermem = calloc(1, SUPERMEMSIZE);

    /* RAM for the structures the system hands out pointers to */
    te->biosram = calloc(1, BIOSRAMSIZE);
    biosram_free = BIOSRAMBASE;
    
    /* Setup a maximum size user RAM */
    te->size = 0xF9FFFF -0x000900;
    te->appmem = calloc(1, te->size);
    
    /* Copy segment sizes from header */
    header = (struct exec_header*)binary;
    te->tsize = endianize_32(header->tsize);
    te->dsize = endianize_32(header->dsize); 
    te->bsize = endianize_32(header->bsize); 
    te->ssize = endianize_32(header->ssize);
    
    /* Ensure that the binary fits in the available user RAM */
    if (te->tsize + te->dsize + te->bsize > te->size)
    {
        printf("Error: Binary too large for the available user RAM\n");
        return -1;
    }

    /* Copy the text and data segments into app memory. The symbol table that
     * follows them in the file is not loaded, TOS only uses it for debugging. */
    memcpy(te->appmem, ((uint8_t*)binary) + sizeof(struct exec_header), te->tsize + te->dsize);

    /* The BSS is zeroed by TOS when loading a program */
    memset(((uint8_t*)te->appmem) + te->tsize + te->dsize, 0, te->bsize);

    /* Allocate basepage */
    te->bp = malloc(sizeof(struct basepage));
    
    /* Prepare basepage according to memory map from ATARI ST/STE Hårdfakta, page 290
     * 
     * Accessible from user mode
     * 
     * 0xFFFFFF - 0xFF8000 I/O-AREA
     * 0xFEFFFF - 0xFC0000 OS ROM
     * 0xFBFFFF - 0xFA0000 CARTRIDGE ROM
     * 0x0FFFFF - 0x000800 USER RAM
     * 0x0007FF - 0x000000 OS RAM
     * 
     * Lay out data like this in USER RAM:
     * 
     * High addresses     HEAP
     * 
     *                   STACK
     * 
     *                    BSS
     * 
     *                    DATA
     * 
     *                    TEXT
     * 
     * Low addresses       BP
     *
     */

    memset(te->bp, 0, sizeof(struct basepage));
    te->bp->p_lowtpa = endianize_32(0x000800);
    te->bp->p_hitpa = endianize_32(te->size + 0x900);
    te->bp->p_tbase = endianize_32(0x000900);
    te->bp->p_tlen = endianize_32(te->tsize);
    te->bp->p_dbase = endianize_32(endianize_32(te->bp->p_tbase) + endianize_32(te->bp->p_tlen));
    te->bp->p_dlen = endianize_32(te->dsize);
    te->bp->p_bbase = endianize_32(endianize_32(te->bp->p_dbase) + endianize_32(te->bp->p_dlen));
    te->bp->p_blen = endianize_32(te->bsize);
    te->bp->p_parent = 0;
    /* TOS defaults the Disk Transfer Address to the command line in the
     * basepage, http://www.yardley.cc/atari/compendium/atari-compendium-chapter-2-GEMDOS.htm#filesystem */
    te->bp->p_dta = endianize_32(0x800 + offsetof(struct basepage, p_cmdlin));
    memcpy(te->bp->p_cmdlin, cmdlin, TOS_CMDLIN_SIZE);
        
    reset_memory();
    add_ptr_memory_area("staticmem0", MEMORY_READWRITE | MEMORY_SUPERWRITE, 0x0, 0x1ff, te->staticmem0);
    add_fnct_memory_area("magicmem0", MEMORY_SUPERREAD, 0x200, 0x2, 0, magic_xbios_supexec_read, magic_xbios_supexec_write);
    add_ptr_memory_area("staticmem1", MEMORY_SUPERREAD | MEMORY_SUPERWRITE, 0x380, 0x600-0x380, te->staticmem1); /* TODO this will probably have to be read using a custom function */
    add_ptr_memory_area("basepage", MEMORY_READWRITE, 0x800, 0x100, te->bp);
    add_ptr_memory_area("userram", MEMORY_READWRITE, 0x900, te->size, te->appmem);
    add_ptr_memory_area("superram", MEMORY_SUPERREAD | MEMORY_SUPERWRITE, 0x600, SUPERMEMSIZE, te->supermem);
    add_ptr_memory_area("biosram", MEMORY_READWRITE, BIOSRAMBASE, BIOSRAMSIZE, te->biosram);

    /* Placing the environment has to wait until the memory areas are
     * registered, as it is written through the emulated memory */
    te->bp->p_env = endianize_32(place_environment(env, env_len));

    /* Relocating must take place after the "userram" has been registered, as
     * it takes place in the memory of the tos machine */
    relocate_program(0x900, binary);

    /* Always a string of its own, so that free_tos_environment can let go of
     * it without having to know where it came from */
    path = getenv("TOS_BASE_PATH");
    if (path == NULL)
        te->base_path = strdup("");
    else
    {
        int n = strlen(path);
        te->base_path = malloc(n + 2);
        if (te->base_path != NULL)
        {
            strcpy(te->base_path, path);
            te->base_path[n] = '/';
            te->base_path[n+1] = 0;
        }
    }

    if (te->base_path == NULL)
        return -1;


    return 0;
}

int init_tos_environment(struct tos_environment *te, void *binary, uint64_t size,
                         const char *cmdlin,
                         const char *env, uint32_t env_len)
{
    if (load_tos_environment(te, binary, size, cmdlin, env, env_len))
        return -1;

    /* Initialize sub-systems */
    gemdos_init(te);
    /* TODO initialization other sub-systems here as well */

    return 0;
}

/*
 * Points the CPU at a freshly loaded application.
 *
 * The application finds its basepage at 4(sp), and a 68000 takes an address
 * error on an odd stack pointer, so the initial user stack is kept even.
 */
static void start_cpu(struct tos_environment *te, uint32_t basepage)
{
    uint32_t sp, pc;
    int i;

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();

    /* A reset leaves the data and address registers as they were, which for an
     * application replacing another one is whatever the one before it happened
     * to be holding. Start it on a machine that has just been switched on. */
    for (i = 0; i < 8; i++)
        m68k_set_reg(M68K_REG_D0 + i, 0);
    for (i = 0; i < 7; i++)
        m68k_set_reg(M68K_REG_A0 + i, 0);

    /* TODO is this really correct, or should it be the MSP? If so, why does
     * that not work? */
    m68k_set_reg(M68K_REG_ISP, 0x600); /* supervisor stack pointer */

    if (basepage == 0x800)
    {
        /* The application the machine was built around */
        sp = (te->size - 4) & ~1u;
        pc = 0x900;
    }
    else
    {
        /* A program another one loaded, which owns the block its basepage
         * sits at the foot of. The basepage goes at 4(sp), so the stack has
         * to start a longword further down than the end of that block. */
        sp = m68k_read_disassembler_32(basepage
                 + offsetof(struct basepage, p_hitpa));
        sp = (sp - 8) & ~1u;
        pc = m68k_read_disassembler_32(basepage
                 + offsetof(struct basepage, p_tbase));
    }

    m68k_set_reg(M68K_REG_USP, sp); /* user stack pointer */
    m68k_write_memory_32(sp + 4, basepage);
    m68k_set_reg(M68K_REG_PC, pc); /* Set PC to the binary entry point */
    disable_supervisor_mode();
}

/* The application the loop is to run once the trap that asked for it has
 * unwound, see exec_tos_binary */
static struct {
    int active;
    int replace;      /* Whether a machine has to be built, or one is there */
    uint32_t basepage;
    void *binary;
    uint64_t binary_size;
    char cmdlin[TOS_CMDLIN_SIZE];
    char *env;
    uint32_t env_len;
} pending;

void exec_tos_basepage(uint32_t basepage)
{
    pending.active = 1;
    pending.replace = 0;
    pending.basepage = basepage;

    /* Leave the loop, which is where the CPU can be pointed somewhere else */
    halt_execution();
}

int exec_tos_binary(const char *host_path, const char *cmdlin,
                    char *env, uint32_t env_len)
{
    void *binary;
    uint64_t size;

    binary = map_tos_binary(host_path, &size);
    if (binary == NULL)
    {
        free(env);
        return -1;
    }

    pending.active = 1;
    pending.replace = 1;
    pending.binary = binary;
    pending.binary_size = size;
    memcpy(pending.cmdlin, cmdlin, TOS_CMDLIN_SIZE);
    pending.env = env;
    pending.env_len = env_len;

    /* Leave the loop, which is where the swap can happen */
    halt_execution();

    return 0;
}

/*
 * Replaces the running application with the one exec_tos_binary was given.
 *
 * The emulated machine is built anew, but the process it runs in is not: the
 * file handles, the drive table and the current directory carry over, which is
 * what a TOS child inherits and what an Fforce before a Pexec is for.
 */
static int replace_application(struct tos_environment *te)
{
    int err;

    free_tos_environment(te);

    /* The system RAM went with it, and so did the addresses XBIOS handed out
     * of it */
    xbios_reset();

    /* The application that introduced itself to GEM is gone, and the one
     * replacing it has to introduce itself again */
    gem_reset();

    err = load_tos_environment(te, pending.binary, pending.binary_size,
                               pending.cmdlin,
                               pending.env, pending.env_len);

    gemdos_reinit(te);

    unmap_tos_binary(pending.binary, pending.binary_size);
    free(pending.env);
    memset(&pending, 0, sizeof pending);

    return err;
}

void run_tos_environment(struct tos_environment *te)
{
    uint32_t basepage = 0x800; /* The application the machine was built for */

    for (;;)
    {
        start_cpu(te, basepage);

        keepongoing = 1;
        while (keepongoing)
            m68k_execute(1);

        /* Stopping means the application is done, unless it stopped in order
         * to hand the machine over to another one */
        if (!pending.active)
            break;

        pending.active = 0;

        if (pending.replace)
        {
            if (replace_application(te))
            {
                printf("Error: failed to start the program Pexec asked for\n");
                break;
            }

            basepage = 0x800;
        }
        else
            basepage = pending.basepage;
    }
}

void free_tos_environment(struct tos_environment *te)
{
    /* Clean up sub-systems */
    gemdos_free();
    /* TODO clean up after other sub-systems here as well */

    free(te->bp);
    te->bp = 0;
    
    free(te->appmem);
    te->appmem = 0;
    
    free(te->supermem);
    te->supermem = 0;

    free(te->biosram);
    te->biosram = 0;

    free(te->staticmem0);
    te->staticmem0 = 0;

    free(te->staticmem1);
    te->staticmem1 = 0;

    free(te->base_path);
    te->base_path = 0;

    reset_memory();
}

/* Invoked upon trap instructions */

void m68k_trap(unsigned int vector)
{
    switch(vector)
    {
        case 0x21: /* trap #$1, GEMDOS */
            gemdos_trap();
            break;
        case 0x22: /* trap #$2, AES / VDI */
            gem_trap();
            break;
        case 0x2d: /* trap #$d, BIOS */
            bios_trap();
            break;
        case 0x2e: /* trap #$e, XBIOS */
            xbios_trap();
            break;
        default:
            halt_execution();
            printf("Invoked unsupported trap 0x%x, this should never happen!\n", vector);
            break;
    }
}

void halt_execution()
{
    keepongoing = 0;
}
