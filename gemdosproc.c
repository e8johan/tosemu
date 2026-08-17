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

/*
 * Process handling, mapped onto the processes of the host.
 *
 * A TOS application owns the whole of the emulated machine, and everything the
 * emulator knows about it - the memory map, the allocator, the handle table -
 * is one set of variables. Two applications cannot share that, so a Pexec
 * forks, and the child throws away the machine it inherited and builds a new
 * one around the program it was asked to run.
 *
 * That also settles what a child inherits, and it lands close to what TOS
 * does: the file handles and their positions carry over, which is what an
 * Fforce before a Pexec is for, and so does the current directory. What
 * belongs to the machine rather than to the process does not, so a child gets
 * its own memory, its own DTA, and its own screen. The current directory a
 * child moves to does not follow back to the parent, which is where this
 * parts with TOS and follows MiNT.
 */

#include "gemdosproc_p.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/limits.h>

#include "m68k.h"
#include "cpu.h"
#include "files.h"
#include "tossystem.h"

#include "gemdos_p.h"

/* Pexec modes, as named in mint/ostruct.h */
#define PE_LOADGO       (0)
#define PE_LOAD         (3)
#define PE_GO           (4)
#define PE_CBASEPAGE    (5)
#define PE_GO_FREE      (6)
#define PE_OVERLAY      (200)

/*
 * Where a child reports the value it terminated with.
 *
 * A TOS program returns a word and Pexec hands it back whole, but the exit
 * status of a host process carries only eight bits of it. The child writes the
 * value here instead, and the parent reads it once the child is gone.
 */
static int exit_code_fd = -1;

/* Reads a zero terminated string out of the emulated memory */
static void get_string(char *buf, int size, uint32_t address)
{
    int i;

    for (i = 0; i < size-1; i++)
    {
        buf[i] = m68k_read_disassembler_8(address + i);
        if (buf[i] == 0)
            return;
    }

    buf[size-1] = 0;
}

/*
 * The environment a child is to run with.
 *
 * A null pointer means the child inherits the one the caller has, which is
 * what every mode taking an environment does. Returns a block the caller
 * frees, or NULL.
 */
static char *child_environment(uint32_t addr, uint32_t *len)
{
    if (addr == 0)
        addr = m68k_read_disassembler_32(0x800 + 0x2c); /* p_env */

    return tos_environment(addr, len);
}

/* Terminates the application, reporting the value to whoever ran it */
static void terminate(uint16_t code)
{
    if (exit_code_fd >= 0)
    {
        uint32_t value = code;

        if (write(exit_code_fd, &value, sizeof value) != (ssize_t)sizeof value)
        {
            /* Nothing to be done from here. The parent reports that the child
             * did not come back rather than inventing a value for it. */
        }
    }

    /* exit rather than _exit, the files the application wrote through the C
     * library still have to reach the disk */
    exit(code & 0xff);
}

uint32_t GEMDOS_Pterm()
{
    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", peek_u16(2));
    }

    terminate(peek_u16(2));

    return 0;
}

uint32_t GEMDOS_Pterm0()
{
    FUNC_TRACE_ENTER

    terminate(0);

    return 0;
}

uint32_t GEMDOS_Pgetpid()
{
    FUNC_TRACE_ENTER

    return getpid();
}

uint32_t GEMDOS_Pgetppid()
{
    FUNC_TRACE_ENTER

    return getppid();
}

/*
 * Runs a program and waits for it, which is Pexec mode 0.
 *
 * Takes ownership of env.
 */
static uint32_t pexec_loadgo(const char *host_path, const char *cmdlin,
                             char *env, uint32_t env_len)
{
    uint32_t code;
    int fds[2];
    int status;
    pid_t pid;
    ssize_t n;

    /* A child starts out with a copy of whatever its parent has buffered, and
     * would write every pending byte of it a second time */
    fflush(NULL);

    if (pipe(fds) != 0)
    {
        free(env);
        return GEMDOS_ENSMEM;
    }

    pid = fork();
    if (pid < 0)
    {
        close(fds[0]);
        close(fds[1]);
        free(env);
        return GEMDOS_ENSMEM;
    }

    if (pid == 0)
    {
        /* The child hands its machine over to the new program. Execution
         * carries on until the trap has unwound, and the loop takes it from
         * there, so this returns like any other GEMDOS call. */
        close(fds[0]);
        exit_code_fd = fds[1];

        if (exec_tos_binary(host_path, cmdlin, env, env_len))
            terminate(0); /* Reported to the parent as EPLFMT below */

        return GEMDOS_E_OK;
    }

    close(fds[1]);
    free(env);

    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;

    /* Read only once the child is gone, so that everything it wrote has been
     * written. Four bytes never fill a pipe, so it cannot have blocked. */
    n = read(fds[0], &code, sizeof code);
    close(fds[0]);

    if (n != (ssize_t)sizeof code)
    {
        /* The child never reached Pterm. It ran into something the emulator
         * does not implement, or a signal took it. */
        return GEMDOS_EPLFMT;
    }

    /* A program's return value is a word, and Pexec reports it with the high
     * word clear. Only a failure of Pexec itself is negative. */
    return code & 0xFFFF;
}

uint32_t GEMDOS_Pexec()
{
    char path[PATH_MAX+1];
    char host[PATH_MAX+1];
    char cmdlin[TOS_CMDLIN_SIZE];
    char *env;
    uint32_t env_len;
    int32_t err;
    int i;

    uint16_t mode = peek_u16(2);
    uint32_t prog = peek_u32(4);
    uint32_t tail = peek_u32(8);
    uint32_t envp = peek_u32(12);

    FUNC_TRACE_ENTER_ARGS {
        printf("    mode: %d, prog: 0x%x, tail: 0x%x, env: 0x%x\n",
               mode, prog, tail, envp);
    }

    switch (mode)
    {
    case PE_LOADGO:
    case PE_OVERLAY:
        break;
    default:
        /* The loading and the asynchronous modes are not here yet */
        return GEMDOS_EINVFN;
    }

    memset(path, 0, sizeof path);
    memset(host, 0, sizeof host);
    get_string(path, sizeof path, prog);

    err = tos_path_to_host(path, host);
    if (err)
        return err;

    if (access(host, R_OK) != 0)
        return GEMDOS_EFILNF;

    /* The command line the child is to find in its basepage, carried over as
     * it stands: its length byte may be the 127 that says the arguments went
     * into the environment instead, which is not a length to recompute. The
     * text is read up to its zero rather than up to that byte, so that reading
     * it cannot run off the end of what the caller set aside for it. */
    memset(cmdlin, 0, sizeof cmdlin);
    cmdlin[0] = m68k_read_disassembler_8(tail);
    for (i = 0; i < TOS_CMDLIN_MAX; i++)
    {
        cmdlin[1+i] = m68k_read_disassembler_8(tail + 1 + i);
        if (cmdlin[1+i] == 0)
            break;
    }

    env = child_environment(envp, &env_len);
    if (env == NULL)
        return GEMDOS_ENSMEM;

    FUNC_TRACE_ARGS {
        printf("    path: '%s' -> '%s'\n", path, host);
        printf("    cmdlin: %d '%s'\n", cmdlin[0], cmdlin+1);
    }

    if (mode == PE_OVERLAY)
    {
        /* Replaces the application without a process in between, so this only
         * ever returns when the program could not be loaded */
        if (exec_tos_binary(host, cmdlin, env, env_len))
            return GEMDOS_EPLFMT;

        return GEMDOS_E_OK;
    }

    return pexec_loadgo(host, cmdlin, env, env_len);
}
