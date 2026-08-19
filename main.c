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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "m68k.h"

#include "tossystem.h"

int verbose;

void cpu_instr_callback()
{
    static char buff[100];
    static unsigned int pc;

    if (verbose)
    {
        pc = m68k_get_reg(NULL, M68K_REG_PC);
        m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
        printf("E %03x: %s\n", pc, buff);
#if 0 /* Dump all regs */
        printf("    D0       D1       D2       D3       D4       D5       D6       D7\n");
        printf("    %08x %08x %08x %08x %08x %08x %08x %08x\n"
            , m68k_get_reg(0, M68K_REG_D0)
            , m68k_get_reg(0, M68K_REG_D1)
            , m68k_get_reg(0, M68K_REG_D2)
            , m68k_get_reg(0, M68K_REG_D3)
            , m68k_get_reg(0, M68K_REG_D4)
            , m68k_get_reg(0, M68K_REG_D5)
            , m68k_get_reg(0, M68K_REG_D6)
            , m68k_get_reg(0, M68K_REG_D7));
        printf("    A0       A1       A2       A3       A4       A5       A6       A7\n");
        printf("    %08x %08x %08x %08x %08x %08x %08x %08x\n"
            , m68k_get_reg(0, M68K_REG_A0)
            , m68k_get_reg(0, M68K_REG_A1)
            , m68k_get_reg(0, M68K_REG_A2)
            , m68k_get_reg(0, M68K_REG_A3)
            , m68k_get_reg(0, M68K_REG_A4)
            , m68k_get_reg(0, M68K_REG_A5)
            , m68k_get_reg(0, M68K_REG_A6)
            , m68k_get_reg(0, M68K_REG_A7));
#endif
        fflush(stdout);
    }
}

/*
 * What this application is called, which is the name of the program with the
 * directory and the extension taken off, in capitals and eight characters
 * long. That is what GEM calls an application and what appl_find looks one up
 * by, so it is worked out from the same thing GEM worked it out from.
 */
static char program_name[9] = "        ";

static void remember_program_name(const char *path)
{
    const char *base = strrchr(path, '/');
    int i;

    base = base ? base + 1 : path;

    for (i = 0; i < 8; i++)
    {
        char c = base[i];

        if (c == 0 || c == '.')
            break;

        program_name[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
}

const char *tos_program_name(void)
{
    return program_name;
}

/*
 * Whether the name is an accessory's.
 *
 * The extension is all there is to go on, and it is all the AES had to go on
 * either - it started whatever in the root of the boot drive ended in .ACC.
 * Either case, because a TOS filesystem did not care and the one underneath
 * this might, which is the same rule the daemon reads the directory by.
 */
static int is_an_accessory(const char *path)
{
    size_t len = strlen(path);

    return len >= 4 && strcasecmp(path + len - 4, ".acc") == 0;
}

int main(int argc, char **argv)
{
    void *binary_data;
    uint64_t binary_size;
    struct tos_environment te;
    char cmdlin[TOS_CMDLIN_SIZE];
    char *env;
    uint32_t env_len;
    int argb = 1;

    verbose = 0;

    /* Program usage */
    if (argc < 2)
    {
        printf("Usage: tosemu [-v] <binary> [<args>]\n\n\t<binary> name of binary to execute\n");
        return -1;
    }

    /* Check if we want to be verbose */
    if (argc >= 3 && strcmp("-v", argv[1]) == 0)
    {
        verbose = -1;
        argb++;
    }

    remember_program_name(argv[argb]);
    tos_load_as_accessory(is_an_accessory(argv[argb]));

    binary_data = map_tos_binary(argv[argb], &binary_size);
    if (binary_data == NULL)
        return -1;

    argb++;
    argv += argb;
    argc -= argb;

    /* The command line and the environment the application is started with */
    host_cmdlin(cmdlin, argc, argv);
    env = host_environment(&env_len);
    if (env == NULL)
    {
        printf("Error: failed to build the environment\n");
        unmap_tos_binary(binary_data, binary_size);
        return -1;
    }

    /* Setup a TOS environment for the binary */
    if (init_tos_environment(&te, binary_data, binary_size,
                             cmdlin, env, env_len))
    {
        printf("Error: failed to initialize TOS environment\n");
        free(env);
        unmap_tos_binary(binary_data, binary_size);
        return -1;
    }

    free(env);
    unmap_tos_binary(binary_data, binary_size);

    run_tos_environment(&te);

    /* Clean up */
    free_tos_environment(&te);

    return 0;
}
