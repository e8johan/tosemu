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
#include "settings.h"
#include "config.h"

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
#include "gemdosmem_p.h"
#include "xbios.h"
#include "bios.h"
#include "gem.h"
#include "screen.h"
#include "linea.h"
#include "midi.h"
#include "interrupt.h"
#include "dongle.h"

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

/*
 * How much room the machine's supervisor stack gets.
 *
 * It used to start at 0x600 and grow downwards, which put it straight through
 * the system variables: 0x4BA is the two hundred hertz counter, 0x44E is where
 * the screen is, and everything from 0x380 up is that sort of thing. One
 * exception frame is six bytes and a handler that saves its registers is
 * sixty more, so three deep reached the counter it was very likely there to
 * service.
 *
 * Nothing had noticed because nothing went deep. Supexec runs a short routine
 * and returns, and every system variable was nought anyway, so writing over
 * them wrote nought onto nought. Both of those stop being true the moment
 * something interrupts: a handler is called on this stack, at whatever depth
 * the program it interrupted had already reached, and by then the variables
 * have values somebody is reading.
 *
 * Eight kilobytes, out of the RAM the system owns rather than out of the
 * machine's low memory, so that growing down from it reaches nothing that
 * means anything. An interrupt handler that wants more than this has lost its
 * return address rather than run out of room.
 */
#define SUPERSTACK_SIZE (8192)

/*
 * RAM for structures the system owns rather than the application, see
 * bios_static_alloc. It is out of the TPA so that what the system reserves
 * does not come out of the application's memory, and it is up here rather than
 * anywhere an ST had RAM for the same reason.
 *
 * In the ROM range rather than the cartridge range, which is where it was and
 * which was wrong. The argument for the cartridge range was that no ST has RAM
 * there, and that is true and was never the question: the cartridge port is an
 * address a program can read, and one that read it found the emulator's own
 * supervisor stack, IOREC buffers and _KBDVECS answering. Nothing had noticed
 * because nothing had been plugged in, so every read of the port stopped the
 * emulator before it could reach them.
 *
 * A machine with no ROM in it has nothing at these addresses either, and
 * nothing reads them: TOS is not a ROM here, it is the host, and a program
 * looking for a version number asks _sysbase rather than the ROM it points at.
 * That makes this the quietest sixty four kilobytes in the map - but it is
 * borrowed rather than owned, and if anything ever does want to read the ROM
 * the answer is to take these structures out of the address space altogether
 * rather than to move them along again.
 */
#define BIOSRAMBASE (0xFC0000)
#define BIOSRAMSIZE (0x10000)

/* The most RAM the machine can have, which is where the cartridge range
 * begins: nothing above that address was RAM on any of these machines, so it
 * is the first byte a machine of the largest possible size does not have. */
#define RAM_MAX (CARTRIDGE_BASE_ADDRESS)

/* The least RAM worth handing a program, which is what has to be left over
 * once the screen has been taken off the top. It is well under the smallest
 * machine below, because what it guards against is not a small machine but a
 * screen that has eaten one: a screen as large as a modern display is more
 * than a megabyte of planes, which is the whole of a 520ST. */
#define RAM_FOR_A_PROGRAM (0x20000)

/*
 * How much RAM the machine has, which is a setting because a program of the
 * period was written for a machine that had a particular amount.
 *
 * How much there is decides how large a document or a picture can be, and a
 * program that sizes its own buffers from what Malloc reports behaves
 * differently on one megabyte than on fourteen. It is also the only way to see
 * what one does when memory runs out, which on a machine with fifteen
 * megabytes in it never happens.
 *
 * The sizes are the ones the machines were sold with, named the way a person
 * would say them. One contiguous block starting at zero, which is what an ST,
 * an STE and a Falcon had - a TT's second sort of memory is not a size in this
 * table: TT RAM is another area of the map altogether, at 0x01000000, and
 * Mxalloc answering for it is what would make it real, so what the TT
 * contributes here is its ST RAM and no more.
 *
 * `max` is the default and is not a machine. It is as much as the memory map
 * has room for, and it is the right default because a program given more
 * memory than any Atari had is not a program that goes wrong - whereas one
 * given less than it was written for is - and because it is what tosemu has
 * always handed out. Whoever wants the machine an application was written for
 * says which it was.
 */
static const struct {
    const char *name;
    uint32_t bytes;
} memories[] = {
    { "512k",   512u * 1024 },  /* a 520ST, and half of what a 1040ST had */
    { "1m",    1024u * 1024 },  /* a 1040ST, and a Falcon as it was sold */
    { "2m",    2048u * 1024 },  /* a Mega ST 2, and a TT's ST RAM */
    { "4m",    4096u * 1024 },  /* a Mega ST 4, and the most an STE takes */
    { "14m",  14336u * 1024 },  /* the most a Falcon takes */
    { "max",   RAM_MAX },       /* as much as the memory map has room for */
};

#define MEMORIES (int)(sizeof memories / sizeof memories[0])

/* What the sizes are called, for a complaint about one that is not there */
static void say_the_sizes(void)
{
    int i;

    for (i = 0; i < MEMORIES; i++)
        fprintf(stderr, "%s %s", i ? "," : "", memories[i].name);
}

/*
 * Which of them the machine has, as the address of the first byte above its
 * RAM - which is also how many bytes there are, the RAM starting at zero.
 *
 * A plain number with a k or an m after it is taken as well. The machines are
 * the sizes worth naming rather than the only ones worth having: a 520ST with
 * a third party board in it was whatever somebody soldered into it, and a
 * program being tried at the size where it runs out of memory wants that size
 * and not the one below it.
 */
static uint32_t machine_ram(void)
{
    const char *want = setting("TOSEMU_MEMORY");
    unsigned long long bytes = 0;
    char *end;
    int i;

    if (!want || !*want)
        return RAM_MAX;

    for (i = 0; i < MEMORIES; i++)
        if (strcasecmp(want, memories[i].name) == 0)
            return memories[i].bytes;

    bytes = strtoull(want, &end, 10);

    /*
     * A number and then a k or an m, and the letter is not optional: nobody
     * counts memory in bytes, so a bare 4 is somebody who meant megabytes and
     * would otherwise be handed four bytes and a complaint about how few that
     * is. Scaled before it is multiplied out, so that a number large enough to
     * wrap round on the way is still a number too large.
     */
    if (end == want)
        bytes = 0;
    else if ((*end == 'k' || *end == 'K') && end[1] == '\0')
        bytes = bytes > RAM_MAX ? RAM_MAX + 1ull : bytes * 1024;
    else if ((*end == 'm' || *end == 'M') && end[1] == '\0')
        bytes = bytes > RAM_MAX ? RAM_MAX + 1ull : bytes * 1024 * 1024;
    else
        bytes = 0;

    /* Said and not understood, which is worth a word rather than a machine
     * nobody asked for: how much memory there is is not something an
     * application can be told twice */
    if (bytes == 0)
    {
        fprintf(stderr, "TOSEMU_MEMORY: '%s' is not an amount of memory. "
                        "There is", want);
        say_the_sizes();
        fprintf(stderr, ", or a number of kilobytes or megabytes such as "
                        "640k.\n");

        return RAM_MAX;
    }

    if (bytes > RAM_MAX)
    {
        fprintf(stderr, "TOSEMU_MEMORY: %s is more memory than the map has "
                        "room for, the cartridge range beginning at 0x%lx, so "
                        "the machine gets %luk instead.\n",
                want, (unsigned long)RAM_MAX,
                (unsigned long)(RAM_MAX / 1024));

        return RAM_MAX;
    }

    if (bytes < RAM_FOR_A_PROGRAM)
    {
        fprintf(stderr, "TOSEMU_MEMORY: %s is not enough memory to run "
                        "anything in, so the machine gets %luk, which is the "
                        "least there is any point in.\n",
                want, (unsigned long)(RAM_FOR_A_PROGRAM / 1024));

        return RAM_FOR_A_PROGRAM;
    }

    return (uint32_t)bytes;
}

/* The stack an accessory is started on, which comes out of that RAM because it
 * is not the accessory's - see the stack field of a tos_environment. It only
 * has to last until the accessory points a7 somewhere of its own, which is the
 * first thing one does, so it is small. */
#define ACCESSORY_STACK (1024)

static uint32_t biosram_free;

/*
 * The programs still to be run in this machine, and where the next one goes.
 *
 * A machine can be asked to load more than one program: the residents first,
 * each staying where it is, and then the program somebody wanted. They share
 * one address space, which is what makes a resident worth having - see
 * tos_run_after, and RESIDENT.md for why this is a command line rather than
 * anything the machine arranges for itself.
 */
#define FOLLOWING_MOST (8)

static struct {
    void *binary;
    uint64_t size;
    char cmdlin[TOS_CMDLIN_SIZE];
} following[FOLLOWING_MOST];

static int following_count;
static int following_next;

/* Where the next program's basepage goes, which moves up as programs stay */
static uint32_t resident_floor = 0x800;

/* The environment every program in this machine is handed, placed once when
 * the machine was built */
static uint32_t machine_env;

/* And where the one running now has its basepage */
static uint32_t current_basepage = 0x800;

uint32_t tos_current_basepage(void)
{
    return current_basepage;
}

int tos_run_after(void *binary, uint64_t size, const char *cmdlin)
{
    if (following_count >= FOLLOWING_MOST)
        return 0;

    following[following_count].binary = binary;
    following[following_count].size = size;
    memcpy(following[following_count].cmdlin, cmdlin, TOS_CMDLIN_SIZE);
    following_count++;

    return 1;
}

/* What every exception vector is filled with, and therefore what one still
 * holding it means: nobody has claimed it. See where they are written. */
static uint32_t default_vector;

uint32_t tos_default_vector(void)
{
    return default_vector;
}

/* Where the screen was put in the machine this time round, and how much of it
 * there is. A machine built again for another program gets another one, the
 * same way the BIOS RAM does. */
static uint32_t screen_base;
static uint32_t screen_size;

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

/*
 * How many bytes a screen of this shape takes.
 *
 * A row of a plane is a whole number of words, so the width is rounded up to
 * one rather than divided by sixteen - which every Atari screen was a multiple
 * of, and a screen as large as a display need not be.
 */
static uint32_t screen_bytes(int16_t width, int16_t height, int16_t planes)
{
    return (uint32_t)((width + 15) / 16) * 2u
         * (uint32_t)planes * (uint32_t)height;
}

uint32_t tos_screen_base(void)
{
    return screen_base;
}

uint32_t tos_screen_size(void)
{
    return screen_size;
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

/* Whether what is being loaded is an accessory, see tos_load_as_accessory */
static int as_accessory;

void tos_load_as_accessory(int yes)
{
    as_accessory = yes;
}

/* Builds the emulated machine around a binary, short of the subsystem setup,
 * which differs between the first application and one replacing another */
static int load_tos_environment(struct tos_environment *te, void *binary,
                                uint64_t size,
                                const char *cmdlin,
                                const char *env, uint32_t env_len)
{
    struct exec_header *header;
    const char *path;
    int16_t screen_w, screen_h, screen_planes;
    uint32_t ramtop, screen_area;

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

    /* And the machine's own stack, taken first so that it is always there:
     * everything else this hands out is asked for while a program runs, and a
     * machine with nowhere to put an exception frame is not a machine */
    te->superstack = bios_static_alloc(SUPERSTACK_SIZE);
    
    /*
     * The screen comes off the top of the machine's RAM, which is where the
     * machine kept it: phystop was the top and the screen sat below it, so
     * what a program was given stopped short of one.
     *
     * Nothing is shown here - what reaches a display is what the VDI drew on
     * a surface of the host's, which has no address in the machine at all -
     * but an application that draws without the VDI asks the XBIOS where the
     * screen is and writes as much into the answer as it has been told the
     * screen holds. So this has to be the size of the screen this machine
     * has, and not of some screen: a buffer the size of one an ST had is a
     * buffer such an application writes straight out of.
     *
     * Which screen that is comes from the settings, the same as everywhere
     * else. A daemon deciding on a larger one is the case this can still fall
     * short of, because the machine has to be laid out before there is a
     * program to run, let alone one that has asked a daemon anything.
     *
     * How much RAM there is to take it off the top of is a setting as well,
     * which is why the two are worked out together: a screen the size of a
     * display is more than a small machine has room for at all.
     */
    ramtop = machine_ram();

    screen_mode(&screen_w, &screen_h, &screen_planes);
    screen_size = screen_bytes(screen_w, screen_h, screen_planes);

    if (screen_size == 0 || screen_size + RAM_FOR_A_PROGRAM > ramtop)
    {
        printf("Error: a %dx%d screen of %d planes leaves no room to run "
               "anything in a machine of %luk\n", screen_w, screen_h,
               screen_planes, (unsigned long)(ramtop / 1024));
        return -1;
    }

    /* On a 256 byte boundary, which is where the hardware needed one. What
     * that rounding leaves over is slack above the screen rather than below
     * it, so an application writing the whole of what VgetSize reports has
     * memory under its pen the whole way. */
    screen_base = (ramtop - screen_size) & ~0xffu;
    screen_area = ramtop - screen_base;
    te->screenmem = calloc(1, screen_area);

    /* Which is worth saying for the same reason the screen is, and for one
     * more: max is a setting rather than a number, and the number is what an
     * application has to live in once the screen has come off the top */
    if (verbose >= VERBOSE_CONFIG)
    {
        printf("tosemu: the machine has %luk of memory, and the screen takes "
               "%luk of it\n",
               (unsigned long)(ramtop / 1024),
               (unsigned long)(screen_area / 1024));
        fflush(stdout);
    }

    /* And the rest of it is the application's */
    te->size = screen_base - 0x000900;
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

    /* How much of it the application owns. An accessory is given its
     * basepage and its three segments, which is what the AES allocated
     * before it loaded one; a program is given the lot. */
    if (as_accessory)
        te->tpa_len = TOS_BASEPAGE_SIZE + te->tsize + te->dsize + te->bsize;
    else
        te->tpa_len = (uint32_t)te->size + TOS_BASEPAGE_SIZE;

    /* And what it stands on until it says otherwise. A program stands on the
     * top of its own block; an accessory has no room in its own for a stack
     * and borrows one, as it did from the AES. */
    te->stack = 0x000800 + te->tpa_len;

    if (as_accessory)
    {
        uint32_t borrowed = bios_static_alloc(ACCESSORY_STACK);

        /* Nowhere to borrow from is not worth stopping for: an accessory sets
         * its own stack up almost at once, so the top of its block is only
         * ever the few words before that */
        if (borrowed)
            te->stack = borrowed + ACCESSORY_STACK;
    }

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
     * High addresses    SCREEN, which is the machine's rather than the
     *                           program's and is not in the TPA
     *
     *                    HEAP
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
    te->bp->p_hitpa = endianize_32(0x000800 + te->tpa_len);
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
    /* 0x200 rather than 0x1ff: the last argument is how many bytes there are
     * and not the address of the last one, so a length one short leaves the
     * top byte of the last exception vector outside every area there is. A
     * program reading vector 127 - or copying the table, which is what a
     * debugger does before it puts its own handlers in - would be reading
     * memory the machine says it does not have. */
    add_ptr_memory_area("staticmem0", MEMORY_READWRITE | MEMORY_SUPERWRITE, 0x0, 0x200, te->staticmem0);
    add_fnct_memory_area("magicmem0", MEMORY_SUPERREAD, 0x200, 0x2, 0, magic_xbios_supexec_read, magic_xbios_supexec_write);
    add_ptr_memory_area("staticmem1", MEMORY_SUPERREAD | MEMORY_SUPERWRITE, 0x380, 0x600-0x380, te->staticmem1); /* TODO this will probably have to be read using a custom function */
    add_ptr_memory_area("basepage", MEMORY_READWRITE, 0x800, 0x100, te->bp);
    add_ptr_memory_area("userram", MEMORY_READWRITE, 0x900, te->size, te->appmem);
    add_ptr_memory_area("screen", MEMORY_READWRITE, screen_base, screen_area, te->screenmem);
    add_ptr_memory_area("superram", MEMORY_SUPERREAD | MEMORY_SUPERWRITE, 0x600, SUPERMEMSIZE, te->supermem);
    add_ptr_memory_area("biosram", MEMORY_READWRITE, BIOSRAMBASE, BIOSRAMSIZE, te->biosram);

    /*
     * Somewhere for every exception vector to point.
     *
     * They were all nought, and nought is not a handler. TOS filled the table
     * with addresses in ROM - even the vectors nothing used - and period
     * software leans on that in a way that is easy to miss: a program that
     * installs a handler saves what was there and chains to it, and a program
     * that wants to know whether it is already installed reads the vector and
     * looks at the code around it.
     *
     * Cubase's MROS is the case that showed it. It reads the TRAP #8 vector,
     * looks four bytes before whatever it points at for its own signature, and
     * takes that as "already loaded". With the vector at nought that read is of
     * address 0xFFFFFC, which is not memory, and the machine stops - so Cubase
     * reports that MROS could not be started.
     *
     * An RTS would be wrong: what these are reached by is an exception, and an
     * exception comes back with RTE. Vectors 0 and 1 are left alone, being the
     * stack pointer and program counter a machine starts with rather than
     * anywhere to jump to, and the table stops at 127 because that is as far as
     * the memory below 0x200 goes.
     */
    {
        int vector;

        default_vector = bios_static_alloc(2);

        if (default_vector)
        {
            m68k_write_memory_16(default_vector, 0x4e73); /* RTE */

            for (vector = 2; vector <= 127; vector++)
                m68k_write_memory_32(4 * vector, default_vector);
        }
    }

    /* And the chips that interrupt, if this machine has any. Here rather than
     * with the other sub-systems because what it adds is memory areas, and
     * because a machine built a second time - which is what Pexec does - needs
     * them again: reset_memory above has just taken them away. */
    interrupt_init();

    /*
     * And whatever is plugged into the cartridge port, for the same two
     * reasons: it is a memory area, and a machine built a second time needs it
     * again. Cubase is why this matters rather than an aside - it reads the
     * port from CUBASE.PRG and again from the CUBASE.EXE it Pexecs, and a
     * child that came up with nothing out there would halt where its parent
     * ran.
     *
     * Readable in both modes because the port is: a cartridge is ROM on the
     * bus and an ST lets a program in user mode read it. Writeable because
     * refusing a write halts the emulator, and dongle_area_write throws it
     * away instead - see the note there.
     */
    if (dongle_wanted())
        add_fnct_memory_area("cartridge",
                             MEMORY_READWRITE | MEMORY_SUPERREAD | MEMORY_SUPERWRITE,
                             CARTRIDGE_BASE_ADDRESS, CARTRIDGE_LENGTH, 0,
                             dongle_area_read, dongle_area_write);

    /* Placing the environment has to wait until the memory areas are
     * registered, as it is written through the emulated memory */
    machine_env = place_environment(env, env_len);
    te->bp->p_env = endianize_32(machine_env);

    /* And so does the line-A block, for the same reason. It is handed the
     * screen this machine was built around rather than asking for one of its
     * own, so that what a program reads out of it is the screen it was given */
    linea_init(screen_w, screen_h, screen_planes);

    /* Relocating must take place after the "userram" has been registered, as
     * it takes place in the memory of the tos machine */
    relocate_program(0x900, binary);

    /* Always a string of its own, so that free_tos_environment can let go of
     * it without having to know where it came from */
    path = setting("TOS_BASE_PATH");
    if (path == NULL)
        te->base_path = strdup("");
    else
    {
        /*
         * One separator on the end, and exactly one however the setting was
         * spelled. Everything that decides whether a host path is on the drive
         * does it by comparing this much of the front of that path, and no
         * path the host resolves has two separators in a row - so a base
         * written with a trailing one would end in two here and match nothing
         * at all, which reads as every file in the world being missing.
         */
        size_t n = strlen(path);

        while (n > 0 && path[n-1] == '/')
            --n;

        te->base_path = malloc(n + 2);
        if (te->base_path != NULL)
        {
            memcpy(te->base_path, path, n);
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

    /* The MIDI port, which is opened whether or not anything will use it: what
     * decides is the setting rather than the program, and a port that is only
     * opened when a program first writes would report its troubles in the
     * middle of a piece of music rather than at the start of the run */
    midi_open();

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

    /*
     * The supervisor stack, at the top of the block reserved for it so that it
     * grows down through its own room and not through the system variables.
     *
     * The interrupt stack pointer is the right one and not the master stack
     * pointer, which the question that used to stand here wondered about: a
     * 68000 has one supervisor stack and calls it the ISP. The MSP arrives with
     * the 68020, where the two are told apart by a bit this machine's status
     * register does not have.
     */
    m68k_set_reg(M68K_REG_ISP, te->superstack + SUPERSTACK_SIZE);

    /*
     * And the interrupt mask down to where TOS left it, on a machine that has
     * anything to be interrupted by.
     *
     * A 68000 comes out of reset with the mask at seven, which is everything
     * blocked, and it is the boot ROM's business to lower it once there is
     * somewhere for an interrupt to go. There is no boot ROM here, so the mask
     * stayed at seven and the most carefully installed handler in the world
     * would never have been reached - the chip would raise its line and the
     * processor would decline to look.
     *
     * Three is what TOS ran applications at: it lets through the vertical
     * blank at four and the MFP at six, and blocks the three levels nothing on
     * this machine uses. Left alone on a machine with no interrupts, which has
     * nothing to let through and no reason to differ from what it always did.
     */
    if (interrupt_wanted())
        m68k_set_reg(M68K_REG_SR,
                     (m68k_get_reg(0, M68K_REG_SR) & ~0x0700u) | 0x0300u);

    if (basepage == 0x800)
    {
        /* The application the machine was built around. The basepage goes at
         * 4(sp), the same as for a loaded one, so the stack starts a longword
         * further down than the end of what it stands on. */
        sp = (te->stack - 8) & ~1u;
        pc = 0x900;

        /*
         * And in a0 as well, if it is an accessory, because that is the only
         * place an accessory is given it: the AES jumps straight to the text
         * segment with the basepage in a0 and nothing on the stack - see
         * gotopgm in EmuTOS's gemasm.S - where a program finds it at 4(sp).
         *
         * It is how an accessory knows it is one. A startup that finds an
         * address there and no parent in the basepage it points at takes the
         * accessory path, and one that finds a0 empty is a program however it
         * was named.
         */
        if (as_accessory)
            m68k_set_reg(M68K_REG_A0, basepage);
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
    int follow;       /* Or whether the next program named goes in above the
                       * one that just stayed resident */
    uint32_t basepage;
    void *binary;
    uint64_t binary_size;
    char cmdlin[TOS_CMDLIN_SIZE];
    char *env;
    uint32_t env_len;
} pending;

int tos_stay_resident(uint32_t keep)
{
    uint32_t floor;

    if (following_next >= following_count)
        return 0;

    floor = mem_keep(current_basepage, keep);

    if (!floor)
        return 0;

    resident_floor = floor;

    pending.active = 1;
    pending.replace = 0;
    pending.follow = 1;

    /* Leave the loop, which is where the next program can be put in */
    halt_execution();

    return 1;
}

void exec_tos_basepage(uint32_t basepage)
{
    pending.active = 1;
    pending.replace = 0;
    pending.follow = 0;
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

    /* Whatever this one is called, it is a program: an accessory is loaded by
     * the AES and nothing else, and Pexec is not the AES */
    as_accessory = 0;

    err = load_tos_environment(te, pending.binary, pending.binary_size,
                               pending.cmdlin,
                               pending.env, pending.env_len);

    gemdos_reinit(te);

    unmap_tos_binary(pending.binary, pending.binary_size);
    free(pending.env);
    memset(&pending, 0, sizeof pending);

    return err;
}

/*
 * Puts the next program named on the command line into the machine, above
 * whatever the one before it kept.
 *
 * Everything it needs is already here: place_program builds a basepage
 * anywhere, mem_claim says the block is spoken for so that Malloc does not
 * hand it out, and start_cpu reads where to start from the basepage. What is
 * new is only that a machine can be asked to do it more than once.
 *
 * Answers the basepage to run, or 0 having said why not.
 */
static uint32_t bring_in_the_next_program(struct tos_environment *te)
{
    uint32_t top = 0x900 + (uint32_t)te->size;
    uint32_t base = resident_floor;
    uint32_t len;
    int32_t placed;
    int i;

    i = following_next++;

    if (base >= top || top - base < TOS_BASEPAGE_SIZE)
    {
        printf("tosemu: the programs that stayed resident have left no room "
               "for the one that was to run after them\n");
        return 0;
    }

    len = top - base;

    if (!mem_claim(base, len))
    {
        printf("tosemu: there is already something where the next program "
               "would go, at 0x%x\n", base);
        return 0;
    }

    placed = place_program(base, len, following[i].binary, following[i].size,
                           following[i].cmdlin, machine_env, 0);

    if (placed != TOS_LOAD_OK)
    {
        printf("tosemu: the next program could not be loaded at 0x%x (%s)\n",
               base, placed == TOS_LOAD_NOROOM ? "no room for it"
                                               : "not a program");
        return 0;
    }

    return base;
}

void run_tos_environment(struct tos_environment *te)
{
    uint32_t basepage = 0x800; /* The application the machine was built for */

    for (;;)
    {
        current_basepage = basepage;

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
        else if (pending.follow)
        {
            /* The one that just ran is staying where it is, and the next one
             * named goes in above it - see tos_stay_resident */
            pending.follow = 0;

            basepage = bring_in_the_next_program(te);

            if (!basepage)
                break;
        }
        else
            basepage = pending.basepage;
    }
}

void free_tos_environment(struct tos_environment *te)
{
    /* Clean up sub-systems */
    gemdos_free();

    /* And the MIDI port, which sends what is still waiting before it goes. The
     * last thing a program does on the way out is silence what it started, and
     * dropping that would leave a note sounding after the program had gone */
    midi_close();

    /* TODO clean up after other sub-systems here as well */

    free(te->bp);
    te->bp = 0;
    
    free(te->appmem);
    te->appmem = 0;

    free(te->screenmem);
    te->screenmem = 0;

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

int execution_halted()
{
    return !keepongoing;
}
