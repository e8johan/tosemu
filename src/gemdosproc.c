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
#include "gem_p.h"

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
#include "gemdosmem_p.h"

/* Pexec modes, as named in mint/ostruct.h */
#define PE_LOADGO       (0)
#define PE_LOAD         (3)
#define PE_GO           (4)
#define PE_CBASEPAGE    (5)
#define PE_GO_FREE      (6)
#define PE_BASEPAGEFLAGS (7)
#define PE_ASYNC_LOADGO (100)
#define PE_ASYNC_GO     (104)
#define PE_ASYNC_GO_FREE (106)
#define PE_OVERLAY      (200)

/* An environment pointer of -1 asks for a process with no environment at all,
 * where a null one asks to inherit the caller's */
#define ENV_NONE        (0xFFFFFFFFu)

/*
 * Where a child reports the value it terminated with.
 *
 * A TOS program returns a word and Pexec hands it back whole, but the exit
 * status of a host process carries only eight bits of it. The child writes the
 * value here instead, and the parent reads it once the child is gone.
 */
static int exit_code_fd = -1;

/*
 * The process id an application is told about.
 *
 * A TOS process id is a word where a host one is not, so what goes out is the
 * host id narrowed to fit. Two processes on a busy machine can be given the
 * same one, which is as good as a sixteen bit id gets.
 */
static int16_t tos_pid(pid_t pid)
{
    return (int16_t)(pid & 0x7FFF);
}

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

/* The environment of the application that called us */
static uint32_t caller_environment(void)
{
    return m68k_read_disassembler_32(0x800 + 0x2c); /* p_env */
}

/*
 * The environment a child is to run with, as a block that outlives the machine
 * it was read out of. Returns one the caller frees, or NULL.
 */
static char *child_environment(uint32_t addr, uint32_t *len)
{
    if (addr == ENV_NONE)
    {
        char *block = malloc(1);

        if (block == NULL)
            return NULL;

        block[0] = 0;
        *len = 1;

        return block;
    }

    if (addr == 0)
        addr = caller_environment();

    return tos_environment(addr, len);
}

/*
 * The environment a basepage is to name.
 *
 * Nothing is copied here. The block belongs to whoever asked for the basepage,
 * and it and the program that will run with it are looking at the same memory,
 * so it has to still be there when the program starts.
 */
static uint32_t basepage_environment(uint32_t addr)
{
    if (addr == ENV_NONE)
        return 0;

    if (addr == 0)
        return caller_environment();

    return addr;
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

    return tos_pid(getpid());
}

uint32_t GEMDOS_Pgetppid()
{
    FUNC_TRACE_ENTER

    return tos_pid(getppid());
}

/*
 * The children that were started by a mode which does not wait for them, and
 * the pipe each of them is to report its return value on.
 */
#define CHILDREN (16)

static struct child {
    pid_t pid;
    int codefd;
    uint32_t block; /* Memory to release once it has been collected, or 0 */
} children[CHILDREN];

static struct child *find_child(pid_t pid)
{
    int i;

    for (i = 0; i < CHILDREN; i++)
        if (children[i].pid == pid)
            return &children[i];

    return NULL;
}

/*
 * Starts a child.
 *
 * Returns 0 in the child, which is the one that carries on into the program,
 * or its process id in the parent along with the pipe to read the return value
 * off. A negative answer means no child was started.
 */
static pid_t start_child(int *codefd)
{
    int fds[2];
    pid_t pid;

    /* A child starts out with a copy of whatever its parent has buffered, and
     * would write every pending byte of it a second time */
    fflush(NULL);

    if (pipe(fds) != 0)
        return -1;

    pid = fork();
    if (pid < 0)
    {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    if (pid == 0)
    {
        close(fds[0]);
        exit_code_fd = fds[1];

        /*
         * A child has a copy of everything the parent had open, and two of the
         * things it copied are shared: the connection showing the parent's
         * windows, and the socket the daemon knows the parent by. Reading
         * either from two processes gets each of them half the messages, and
         * closing either properly would take something away from the parent.
         *
         * So they are let go of rather than closed, before the child is
         * anything at all. What it opens afterwards is its own, and it is a
         * different application from then on.
         */
        gem_forget();

        return 0;
    }

    close(fds[1]);
    *codefd = fds[0];

    return pid;
}

/*
 * Waits for a child that has been started and reports what it left with.
 *
 * Reading the pipe only once the child is gone means everything it wrote has
 * been written, and four bytes never fill a pipe, so it cannot have blocked.
 */
static uint32_t collect_child(pid_t pid, int codefd, int *terminated)
{
    uint32_t code;
    int status;
    ssize_t n;

    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;

    n = read(codefd, &code, sizeof code);
    close(codefd);

    /* Nothing arriving means the child never reached Pterm: it ran into
     * something the emulator does not implement, or a signal took it */
    *terminated = (n == (ssize_t)sizeof code);

    return *terminated ? (code & 0xFFFF) : 0;
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
    int codefd = -1;
    int terminated;
    pid_t pid;

    pid = start_child(&codefd);
    if (pid < 0)
    {
        free(env);
        return GEMDOS_ENSMEM;
    }

    if (pid == 0)
    {
        /* The child hands its machine over to the new program. Execution
         * carries on until the trap has unwound, and the loop takes it from
         * there, so this returns like any other GEMDOS call. */
        if (exec_tos_binary(host_path, cmdlin, env, env_len))
            terminate(0); /* Reported to the parent as EPLFMT below */

        return GEMDOS_E_OK;
    }

    free(env);

    code = collect_child(pid, codefd, &terminated);
    if (!terminated)
        return GEMDOS_EPLFMT;

    /* A program's return value is a word, and Pexec reports it with the high
     * word clear. Only a failure of Pexec itself is negative. */
    return code;
}

/*
 * Runs a program that another one has already loaded, which is Pexec mode 4,
 * and mode 6 when the memory it was given goes back afterwards.
 */
static uint32_t pexec_go(uint32_t basepage, int release)
{
    uint32_t code;
    int codefd = -1;
    int terminated;
    pid_t pid;

    pid = start_child(&codefd);
    if (pid < 0)
        return GEMDOS_ENSMEM;

    if (pid == 0)
    {
        /* The program is already in the memory the child inherited, so there
         * is nothing to load, only somewhere else to point the CPU */
        exec_tos_basepage(basepage);

        return GEMDOS_E_OK;
    }

    code = collect_child(pid, codefd, &terminated);

    if (release)
        mem_free(basepage);

    if (!terminated)
        return GEMDOS_EPLFMT;

    return code;
}

/*
 * Starts a program without waiting for it, which is what the MiNT modes do,
 * and answers with its process id.
 *
 * Takes ownership of env when there is one.
 */
static uint32_t pexec_async(const char *host_path, const char *cmdlin,
                            char *env, uint32_t env_len,
                            uint32_t basepage, int release)
{
    struct child *slot;
    int codefd = -1;
    pid_t pid;

    /* Nowhere left to remember one, so it could never be collected */
    slot = find_child(0);
    if (slot == NULL)
    {
        free(env);
        return GEMDOS_ENSMEM;
    }

    pid = start_child(&codefd);
    if (pid < 0)
    {
        free(env);
        return GEMDOS_ENSMEM;
    }

    if (pid == 0)
    {
        /* A child of an asynchronous mode has no children of its own to
         * account for, whatever its parent was keeping track of */
        memset(children, 0, sizeof children);

        if (host_path)
        {
            if (exec_tos_binary(host_path, cmdlin, env, env_len))
                terminate(0);
        }
        else
            exec_tos_basepage(basepage);

        return GEMDOS_E_OK;
    }

    free(env);

    slot->pid = pid;
    slot->codefd = codefd;
    slot->block = release ? basepage : 0;

    return tos_pid(pid);
}

/*
 * Collects a child that one of the asynchronous modes started.
 *
 * MiNT answers with the process id in the high word and what the child left
 * with in the low one: the return value moved up a byte, or the signal that
 * took it. Only eight bits of a return value fit there, where Pexec mode 0
 * reports the whole word.
 */
static uint32_t wait_for_child(int16_t want, int nohang)
{
    struct child *slot;
    uint32_t code = 0;
    int status, i;
    pid_t waiting = -1;
    pid_t pid;
    ssize_t n;

    for (i = 0; i < CHILDREN; i++)
        if (children[i].pid > 0 &&
            (want <= 0 || tos_pid(children[i].pid) == want))
            waiting = want > 0 ? children[i].pid : 0;

    if (waiting < 0)
        return GEMDOS_EFILNF; /* There is no such child to wait for */

    /* Any of them will do unless one was named, and the host knows a child by
     * a different id than the one the application was given */
    do
        pid = waitpid(waiting > 0 ? waiting : -1, &status,
                      nohang ? WNOHANG : 0);
    while (pid < 0 && errno == EINTR);

    if (pid == 0)
        return 0; /* Asked not to wait, and none of them has finished */

    if (pid < 0)
        return GEMDOS_EFILNF;

    slot = find_child(pid);
    if (slot == NULL)
        return GEMDOS_EFILNF;

    n = read(slot->codefd, &code, sizeof code);
    close(slot->codefd);

    if (slot->block)
        mem_free(slot->block);

    memset(slot, 0, sizeof *slot);

    if (n == (ssize_t)sizeof code)
        return ((uint32_t)tos_pid(pid) << 16) | ((code & 0xff) << 8);

    /* It never reached Pterm, so the host is all there is to go on */
    if (WIFSIGNALED(status))
        return ((uint32_t)tos_pid(pid) << 16) | (WTERMSIG(status) & 0x7f);

    return ((uint32_t)tos_pid(pid) << 16) | ((WEXITSTATUS(status) & 0xff) << 8);
}

uint32_t GEMDOS_Pwait()
{
    FUNC_TRACE_ENTER

    return wait_for_child(-1, 0);
}

uint32_t GEMDOS_Pwait3()
{
    int16_t flag = peek_s16(2);

    FUNC_TRACE_ENTER_ARGS {
        printf("    flag: %d\n", flag);
    }

    /* Bit 1 asks not to wait. The resource usage a caller may also ask for is
     * left alone, tosemu has nothing to fill it in with. */
    return wait_for_child(-1, flag & 1);
}

uint32_t GEMDOS_Pwaitpid()
{
    int16_t pid = peek_s16(2);
    int16_t flag = peek_s16(4);

    FUNC_TRACE_ENTER_ARGS {
        printf("    pid: %d, flag: %d\n", pid, flag);
    }

    return wait_for_child(pid, flag & 1);
}

/* Reads the command line a caller is handing over.
 *
 * It is carried across as it stands: its length byte may be the 127 that says
 * the arguments went into the environment instead, which is not a length to
 * recompute. The text is read up to its zero rather than up to that byte, so
 * that reading it cannot run off the end of what the caller set aside.
 */
static void get_cmdlin(char *field, uint32_t tail)
{
    int i;

    memset(field, 0, TOS_CMDLIN_SIZE);
    field[0] = m68k_read_disassembler_8(tail);

    for (i = 0; i < TOS_CMDLIN_MAX; i++)
    {
        field[1+i] = m68k_read_disassembler_8(tail + 1 + i);
        if (field[1+i] == 0)
            break;
    }
}

/* Turns the name of a program into one the host knows, and refuses one that is
 * not there before anything else is set up for it */
static int32_t get_program(char *host, uint32_t prog)
{
    char path[PATH_MAX+1];
    int32_t err;

    memset(path, 0, sizeof path);
    get_string(path, sizeof path, prog);

    err = tos_path_to_host(path, host);
    if (err)
        return err;

    if (access(host, R_OK) != 0)
        return GEMDOS_EFILNF;

    return GEMDOS_E_OK;
}

/*
 * Sets aside memory for a program and builds its basepage, which is Pexec
 * modes 3, 5 and 7: loading one without running it, and making room for one
 * that is not there yet.
 *
 * Returns the basepage, or a negative GEMDOS error.
 */
static uint32_t pexec_load(const char *host_path, const char *cmdlin,
                           uint32_t env)
{
    void *binary = NULL;
    uint64_t size = 0;
    uint32_t base, len;
    int32_t err;

    if (host_path)
    {
        binary = map_tos_binary(host_path, &size);
        if (binary == NULL)
            return GEMDOS_EPLFMT;
    }

    /* TOS hands the new basepage everything that is free and leaves it to the
     * caller to Mshrink it back down */
    len = mem_largest_free();
    base = len ? mem_alloc(len) : 0;
    if (base == 0)
    {
        if (binary)
            unmap_tos_binary(binary, size);

        return GEMDOS_ENSMEM;
    }

    err = place_program(base, len, binary, size, cmdlin, env, 0x800);

    if (binary)
        unmap_tos_binary(binary, size);

    if (err != TOS_LOAD_OK)
    {
        mem_free(base);

        return err == TOS_LOAD_NOROOM ? GEMDOS_ENSMEM : GEMDOS_EPLFMT;
    }

    return base;
}

uint32_t GEMDOS_Pexec()
{
    char host[PATH_MAX+1];
    char cmdlin[TOS_CMDLIN_SIZE];
    char *env;
    uint32_t env_len;
    int32_t err;

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
    /* Running a program that is already loaded. The basepage says where it is
     * and how much memory goes with it, so there is nothing else to read. */
    case PE_GO:
        return pexec_go(tail, 0);
    case PE_GO_FREE:
        return pexec_go(tail, 1);
    case PE_ASYNC_GO:
        return pexec_async(NULL, NULL, NULL, 0, tail, 0);
    case PE_ASYNC_GO_FREE:
        return pexec_async(NULL, NULL, NULL, 0, tail, 1);

    /* Making room for a program without loading one. Mode 7 is the same with
     * program flags, which tosemu acts on none of. */
    case PE_CBASEPAGE:
    case PE_BASEPAGEFLAGS:
        get_cmdlin(cmdlin, tail);

        return pexec_load(NULL, cmdlin, basepage_environment(envp));

    /* Loading a program, with or without running it */
    case PE_LOAD:
    case PE_LOADGO:
    case PE_ASYNC_LOADGO:
    case PE_OVERLAY:
        break;

    default:
        return GEMDOS_EINVFN;
    }

    memset(host, 0, sizeof host);
    err = get_program(host, prog);
    if (err)
        return err;

    get_cmdlin(cmdlin, tail);

    FUNC_TRACE_ARGS {
        printf("    path: -> '%s'\n", host);
        printf("    cmdlin: %d '%s'\n", cmdlin[0], cmdlin+1);
    }

    if (mode == PE_LOAD)
        return pexec_load(host, cmdlin, basepage_environment(envp));

    env = child_environment(envp, &env_len);
    if (env == NULL)
        return GEMDOS_ENSMEM;

    if (mode == PE_OVERLAY)
    {
        /* Replaces the application without a process in between, so this only
         * ever returns when the program could not be loaded */
        if (exec_tos_binary(host, cmdlin, env, env_len))
            return GEMDOS_EPLFMT;

        return GEMDOS_E_OK;
    }

    if (mode == PE_ASYNC_LOADGO)
        return pexec_async(host, cmdlin, env, env_len, 0, 0);

    return pexec_loadgo(host, cmdlin, env, env_len);
}


/*
 * Starting a program alongside this one, which is what the AES's shel_write
 * does.
 *
 * It is Pexec's asynchronous mode with the arguments coming from somewhere
 * else: the AES hands over a path and a command line the same way GEMDOS does,
 * and what has to happen to them is the same. Putting it here rather than in
 * the AES is what keeps one copy of it - resolving a TOS path, building a
 * command line field, and forking a child that lets go of what it inherited are
 * all things this file already knows how to do.
 */
uint32_t tos_start_program(uint32_t prog, uint32_t tail)
{
    char host[PATH_MAX+1];
    char cmdlin[TOS_CMDLIN_SIZE];
    char *env;
    uint32_t env_len;
    int32_t err;

    memset(host, 0, sizeof host);
    err = get_program(host, prog);
    if (err)
        return (uint32_t)err;

    get_cmdlin(cmdlin, tail);

    env = child_environment(ENV_NONE, &env_len);
    if (env == NULL)
        return GEMDOS_ENSMEM;

    return pexec_async(host, cmdlin, env, env_len, 0, 0);
}
