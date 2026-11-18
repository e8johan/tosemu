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

#include "gemdos.h"

#include "gemdosmem_p.h"
#include "gemdoscon_p.h"
#include "gemdosfile_p.h"
#include "gemdosproc_p.h"

#include <stdlib.h>
#include <time.h>

#include "tossystem.h"
#include "calltrace.h"
#include "cpu.h"
#include "m68k.h"
#include "utils.h"

#include "gemdos_p.h"
#include "gemdosnet_p.h"
#include "sting.h"

/* GEMDOS functions */

/* Date/time functions *******************************************************/

uint32_t GEMDOS_Tgetdate()
{
    /*
    * 0-4     Day (1-31)
    * 5-8     Month (1-12)
    * 9-15    Year (0-119, 0= 1980)
    * 
    * http://toshyp.atari.org/en/00500a.html
    */
    uint32_t res = 0;
    time_t t;
    struct tm *lt;

    FUNC_TRACE_ENTER
    
    t = time(NULL);
    lt = localtime(&t);
    
    res = lt->tm_mday |
          ((lt->tm_mon+1) << 5) |
          ((lt->tm_year-80) << 9);
    
    return res;
}

uint32_t GEMDOS_Tgettime()
{
    /*
    * 0-4     Seconds in units of two (0-29)
    * 5-10    Minutes (0-59)
    * 11-15   Hours (0-23)
    * 
    * http://toshyp.atari.org/en/00500a.html
    */
    uint32_t res = 0;
    time_t t;
    struct tm *lt;
    
    FUNC_TRACE_ENTER

    t = time(NULL);
    lt = localtime(&t);
    
    res = (lt->tm_sec / 2) |
          (lt->tm_min << 5) |
          (lt->tm_hour << 11);
    
    return res;
}

/* Misc functions ************************************************************/

/* Super changes which mode the caller runs in, and it is the caller that goes
 * on running: it returns to the instruction after the trap, on the stack it
 * was standing on, with everything it had put there still under it.
 *
 * That has to be arranged rather than assumed, because a 68000 keeps two stack
 * pointers in the one register and swaps them when the S bit moves. Setting
 * the bit puts the caller on the stack the system was using, which is not
 * where anything of the caller's is; what TOS does instead is let the caller
 * keep its own stack and hand back the address of the one that was displaced,
 * for it to give back when it is done. So the stack pointer is carried across
 * the switch here, and what the switch displaced is what Super answers with.
 *
 * Which way it goes is decided by the mode the caller is in, not by the
 * argument - see _enter in EmuTOS's bdos/rwa.S, which picks x20_usr or x20_sup
 * on the S bit before it looks at the parameter. From user mode the argument
 * is the supervisor stack to stand on, with nought meaning the caller's own;
 * from supervisor mode it is the one to give back.
 */
uint32_t GEMDOS_Super()
{
    uint32_t lv0 = peek_u32(2);
    uint32_t res = 0;
    uint32_t sp;

    FUNC_TRACE_ENTER_ARGS {
        printf("    0x%x\n", lv0);
    }

    if (lv0 == 1) { /* -1 in supervisor mode, nought in user mode - x20_inq */
        if (is_supervisor_mode_enabled()) {
            res = 0xffffffff;
        } else {
            res = 0;
        }
    } else if (!is_supervisor_mode_enabled()) { /* Set CPU in supervisor mode */
        sp = lv0 ? lv0 : m68k_get_reg(0, M68K_REG_A7);
        res = m68k_get_reg(0, M68K_REG_ISP);
        enable_supervisor_mode();
        m68k_set_reg(M68K_REG_A7, sp);
    } else if (lv0 == 0) {
        /* Back to user mode on the stack the caller is standing on, leaving
         * the supervisor stack where it is - xs_0 in the same file */
        sp = m68k_get_reg(0, M68K_REG_A7);
        disable_supervisor_mode();
        m68k_set_reg(M68K_REG_A7, sp);
    } else { /* Set CPU in user mode, restore the supervisor stack to lv0 */
        /*
         * The user stack pointer is left exactly as it was, and that is the
         * whole of what this call does besides putting the mode back.
         *
         * A 68000 keeps the two stack pointers in separate registers and swaps
         * which one a7 is when the mode changes, so coming back to user mode
         * restores the user one by itself. Writing a7 here would set the user
         * stack pointer to whatever a7 happened to be in supervisor mode - and
         * a program that entered supervisor mode by writing the status
         * register rather than by calling Super has been standing on the
         * machine's supervisor stack, so what it would be handed back is an
         * address inside that. The next rts then returns to whatever is there.
         *
         * Cubase is the case that showed it. It does `move #$2300,sr`, pushes
         * its arguments, calls this to come back out, and returns - and with
         * a7 written here it returned to address nought and ran off through
         * the exception vector table.
         *
         * For a program that paired this with Super(0) the two are the same
         * value, which is why nothing noticed: Super(0) points the supervisor
         * stack at the caller's own, so a balanced program has a7 back where
         * the user stack pointer already was.
         */
        /*
         * A caller that was already in supervisor mode has been standing on
         * the supervisor stack, and everything it pushed for this call is on
         * that stack rather than on the user one it is about to go back to.
         * So four bytes are carried over and the user stack pointer is moved
         * down by them, which is what EmuTOS's x20_sup does - see the
         * `move.l (sp)+,-(a0)` there, and the comment calling it the function
         * number and the parameter.
         *
         * A caller already standing on the user stack - which is what Super(0)
         * arranges, the supervisor stack having been pointed at the caller's
         * own - needs none of it, and EmuTOS skips it for exactly that case.
         * That is the ordinary way round and why nothing noticed before.
         *
         * Cubase is the case that wants it. It enters supervisor mode by
         * writing the status register, calls this to come back out, and then
         * adds twelve to a7 before returning: six for what it pushed, and six
         * for the exception frame a real trap would have left. Without the
         * four bytes carried across, a7 lands past its own return address.
         */
        if (is_supervisor_mode_enabled())
        {
            uint32_t sp = m68k_get_reg(0, M68K_REG_A7);
            uint32_t usp = m68k_get_reg(0, M68K_REG_USP);

            if (sp != usp)
            {
                uint32_t carried = m68k_read_memory_32(sp);

                usp -= 4;
                m68k_write_memory_32(usp, carried);
                m68k_set_reg(M68K_REG_USP, usp);
            }
        }

        m68k_set_reg(M68K_REG_ISP, lv0);
        disable_supervisor_mode();
        res = 0;
    }

    return res;
}

uint32_t GEMDOS_Sversion()
{
    return 0x1500;
}

/* Used to tag Mint-only calls that should not halt execution, but are not implemented */
uint32_t GEMDOS_Unknown();

/* Table of non-implemented GEMDOS functions */

#define GEMDOS_Ssystem GEMDOS_Unknown
#define GEMDOS_Ffstat64 GEMDOS_Unknown
#define GEMDOS_Tgettimeofday GEMDOS_Unknown
#define GEMDOS_Fstat64 GEMDOS_Unknown
#define GEMDOS_Psysctl GEMDOS_Unknown

/* The parts of MiNTnet's sockets that gemdosnet.c leaves out */
#define GEMDOS_Fsocketpair GEMDOS_Unknown
#define GEMDOS_Frecvmsg GEMDOS_Unknown
#define GEMDOS_Fsendmsg GEMDOS_Unknown

#define GEMDOS_Cauxin NULL
#define GEMDOS_Cauxos NULL
#define GEMDOS_Cauxis NULL
#define GEMDOS_Cauxout NULL
#define GEMDOS_Cprnos NULL
#define GEMDOS_Cprnout NULL
#define GEMDOS_Dclosedir NULL
#define GEMDOS_Dcntl NULL
#define GEMDOS_Ddelete NULL
#define GEMDOS_Dgetcwd NULL
#define GEMDOS_Dlock NULL
#define GEMDOS_Dopendir NULL
#define GEMDOS_Dpathconf NULL
#define GEMDOS_Dreaddir NULL
#define GEMDOS_Drewinddir NULL
#define GEMDOS_Fchmod NULL
#define GEMDOS_Fchown NULL
#define GEMDOS_Fgetchar NULL
#define GEMDOS_Finstat NULL
#define GEMDOS_Flink NULL
#define GEMDOS_Flock NULL
#define GEMDOS_Fmidipipe NULL
#define GEMDOS_Foutstat NULL
#define GEMDOS_Fpipe NULL
#define GEMDOS_Fputchar NULL
#define GEMDOS_Freadlink NULL
#define GEMDOS_Fsymlink NULL
#define GEMDOS_Fxattr GEMDOS_Unknown
#define GEMDOS_Maddalt NULL
#define GEMDOS_Pause NULL
#define GEMDOS_Pdomain GEMDOS_Unknown
#define GEMDOS_Pfork NULL
#define GEMDOS_Pgetegid GEMDOS_Unknown
#define GEMDOS_Pgeteuid GEMDOS_Unknown
#define GEMDOS_Pgetgid GEMDOS_Unknown
#define GEMDOS_Pgetpgrp NULL
#define GEMDOS_Pgetuid GEMDOS_Unknown
#define GEMDOS_Pkill NULL
#define GEMDOS_Pmsg NULL
#define GEMDOS_Pnice NULL
#define GEMDOS_Prenice NULL
#define GEMDOS_Prusage NULL
#define GEMDOS_Psemaphore NULL
#define GEMDOS_Psetgit NULL
#define GEMDOS_Psetlimit NULL
#define GEMDOS_Psetpgrp NULL
#define GEMDOS_Psetuid NULL
#define GEMDOS_Psigaction NULL
#define GEMDOS_Psigblock NULL
#define GEMDOS_Psignal NULL
#define GEMDOS_Psigpause NULL
#define GEMDOS_Psigpending NULL
#define GEMDOS_Psigreturn NULL
#define GEMDOS_Psigsetmask NULL
#define GEMDOS_Pumask GEMDOS_Unknown
#define GEMDOS_Pursval NULL
#define GEMDOS_Pvfork NULL
#define GEMDOS_Salert NULL
#define GEMDOS_Pyield NULL
#define GEMDOS_Sysconf NULL
#define GEMDOS_Talarm NULL
#define GEMDOS_Tsetdate NULL
#define GEMDOS_Tsettime NULL

/* GEMDOS function table according to
 * http://www.yardley.cc/atari/compendium/atari-compendium-GEMDOS-Function-Reference.htm
 * 
 * Added Ssystem from http://toshyp.atari.org/en/00500e.html
 */
struct GEMDOS_function {
    char *name;
    uint32_t (*fnct)();
    uint16_t id;
    const char *args;   /* What -vv shows it was called with - calltrace.h */
};

struct GEMDOS_function GEMDOS_functions[] = {
    {"Cauxin",      GEMDOS_Cauxin, 0x03, ""},
    {"Cauxis",      GEMDOS_Cauxis, 0x12, ""},
    {"Cauxos",      GEMDOS_Cauxos, 0x13, ""},
    {"Cauxout",     GEMDOS_Cauxout, 0x04, "c"},
    {"Cconin",      GEMDOS_Cconin, 0x01, "=k"},
    {"Cconis",      GEMDOS_Cconis, 0x0B, ""},
    {"Cconos",      GEMDOS_Cconos, 0x10, ""},
    {"Cconout",     GEMDOS_Cconout, 0x02, "c"},
    {"Cconrs",      GEMDOS_Cconrs, 0x0A, "p"},
    {"Cconws",      GEMDOS_Cconws, 0x09, "s"},
    {"Cnecin",      GEMDOS_Cnecin, 0x08, "=k"},
    {"Cprnos",      GEMDOS_Cprnos, 0x11, ""},
    {"Cprnout",     GEMDOS_Cprnout, 0x05, "c"},
    {"Crawcin",     GEMDOS_Crawcin, 0x07, "=k"},
    {"Crawio",      GEMDOS_Crawio, 0x06, "x=k"},
    {"Dclosedir",   GEMDOS_Dclosedir, 0x12B, "L"},
    {"Dcntl",       GEMDOS_Dcntl, 0x130, "wsL"},
    {"Dcreate",     GEMDOS_Dcreate, 0x39, "s"},
    {"Ddelete",     GEMDOS_Ddelete, 0x3A, "s"},
    {"Dfree",       GEMDOS_Dfree, 0x36, "pw"},
    {"Dgetcwd",     GEMDOS_Dgetcwd, 0x13B, "pww"},
    {"Dgetdrv",     GEMDOS_Dgetdrv, 0x19, ""},
    {"Dgetpath",    GEMDOS_Dgetpath, 0x47, "pw"},
    {"Dlock",       GEMDOS_Dlock, 0x135, "ww"},
    {"Dopendir",    GEMDOS_Dopendir, 0x128, "sw"},
    {"Dpathconf",   GEMDOS_Dpathconf, 0x124, "sw"},
    {"Dreaddir",    GEMDOS_Dreaddir, 0x129, "wLp"},
    {"Drewinddir",  GEMDOS_Drewinddir, 0x12A, "L"},
    {"Dsetdrv",     GEMDOS_Dsetdrv, 0x0E, "w=x"},
    {"Dsetpath",    GEMDOS_Dsetpath, 0x3B, "s"},
    {"Fattrib",     GEMDOS_Fattrib, 0x43, "sww"},
    {"Fchmod",      GEMDOS_Fchmod, 0x132, "sx"},
    {"Fchown",      GEMDOS_Fchown, 0x131, "sww"},
    {"Fclose",      GEMDOS_Fclose, 0x3E, "w"},
    {"Fcntl",       GEMDOS_Fcntl, 0x104, "wLx"},
    {"Fcreate",     GEMDOS_Fcreate, 0x3C, "sx"},
    {"Fdatime",     GEMDOS_Fdatime, 0x57, "pww"},
    {"Fdelete",     GEMDOS_Fdelete, 0x41, "s"},
    {"Fdup",        GEMDOS_Fdup, 0x45, "w"},
    {"Fforce",      GEMDOS_Fforce, 0x46, "ww"},
    {"Fgetchar",    GEMDOS_Fgetchar, 0x107, "ww"},
    {"Fgetdta",     GEMDOS_Fgetdta, 0x2F, "=a"},
    {"Finstat",     GEMDOS_Finstat, 0x105, "w"},
    {"Flink",       GEMDOS_Flink, 0x12D, "ss"},
    {"Flock",       GEMDOS_Flock, 0x5C, "wwll"},
    {"Fmidipipe",   GEMDOS_Fmidipipe, 0x126, "www"},
    {"Fopen",       GEMDOS_Fopen, 0x3D, "sx"},
    {"Foutstat",    GEMDOS_Foutstat, 0x106, "w"},
    {"Fpipe",       GEMDOS_Fpipe, 0x100, "p"},
    {"Fputchar",    GEMDOS_Fputchar, 0x108, "wlw"},
    {"Fread",       GEMDOS_Fread, 0x3F, "wlp"},
    {"Freadlink",   GEMDOS_Freadlink, 0x12F, "wps"},
    {"Frename",     GEMDOS_Frename, 0x56, "wss"},
    {"Fseek",       GEMDOS_Fseek, 0x42, "lww"},
    {"Fselect",     GEMDOS_Fselect, 0x11D, "wppl"},
    {"Fsetdta",     GEMDOS_Fsetdta, 0x1A, "p"},
    {"Fsfirst",     GEMDOS_Fsfirst, 0x4E, "sx"},
    {"Fsnext",      GEMDOS_Fsnext, 0x4F, ""},
    {"Fsymlink",    GEMDOS_Fsymlink, 0x12E, "ss"},
    {"Fwrite",      GEMDOS_Fwrite, 0x40, "wlp"},
    {"Fxattr",      GEMDOS_Fxattr, 0x12C, "wsp"},
    {"Maddalt",     GEMDOS_Maddalt, 0x14, "pl"},
    {"Malloc",      GEMDOS_Malloc, 0x48, "l=a"},
    {"Mfree",       GEMDOS_Mfree, 0x49, "p"},
    {"Mshrink",     GEMDOS_Mshrink, 0x4A, "wpl"},
    {"Mxalloc",     GEMDOS_Mxalloc, 0x44, "lw=a"},
    {"Pause",       GEMDOS_Pause, 0x121, ""},
    {"Pdomain",     GEMDOS_Pdomain, 0x119, "w"},
    {"Pexec",       GEMDOS_Pexec, 0x4B, "wppp"},
    {"Pfork",       GEMDOS_Pfork, 0x11B, ""},
    {"Pgetegid",    GEMDOS_Pgetegid, 0x139, ""},
    {"Pgeteuid",    GEMDOS_Pgeteuid, 0x138, ""},
    {"Pgetgid",     GEMDOS_Pgetgid, 0x114, ""},
    {"Pgetpgrp",    GEMDOS_Pgetpgrp, 0x10D, ""},
    {"Pgetpid",     GEMDOS_Pgetpid, 0x10B, ""},
    {"Pgetppid",    GEMDOS_Pgetppid, 0x10C, ""},
    {"Pgetuid",     GEMDOS_Pgetuid, 0x10F, ""},
    {"Pkill",       GEMDOS_Pkill, 0x111, "ww"},
    {"Pmsg",        GEMDOS_Pmsg, 0x125, "wLp"},
    {"Pnice",       GEMDOS_Pnice, 0x10A, "w"},
    {"Prenice",     GEMDOS_Prenice, 0x127, "ww"},
    {"Prusage",     GEMDOS_Prusage, 0x11E, "p"},
    {"Psemaphore",  GEMDOS_Psemaphore, 0x134, "wLl"},
    {"Psetgit",     GEMDOS_Psetgit, 0x115, "w"},
    {"Psetlimit",   GEMDOS_Psetlimit, 0x11F, "wl"},
    {"Psetpgrp",    GEMDOS_Psetpgrp, 0x10E, "ww"},
    {"Psetuid",     GEMDOS_Psetuid, 0x110, "w"},
    {"Psigaction",  GEMDOS_Psigaction, 0x137, "wpp"},
    {"Psigblock",   GEMDOS_Psigblock, 0x116, "L"},
    {"Psignal",     GEMDOS_Psignal, 0x112, "wp"},
    {"Psigpause",   GEMDOS_Psigpause, 0x136, "L"},
    {"Psigpending", GEMDOS_Psigpending, 0x123, ""},
    {"Psigreturn",  GEMDOS_Psigreturn, 0x11A, ""},
    {"Psigsetmask", GEMDOS_Psigsetmask, 0x117, "L"},
    {"Pterm",       GEMDOS_Pterm, 0x4C, "w=-"},
    {"Pterm0",      GEMDOS_Pterm0, 0x0, "=-"},
    {"Ptermres",    GEMDOS_Ptermres, 0x31, "lw=-"},
    {"Pumask",      GEMDOS_Pumask, 0x133, "x"},
    {"Pursval",     GEMDOS_Pursval, 0x118, "l"},
    {"Pvfork",      GEMDOS_Pvfork, 0x113, ""},
    {"Pwait",       GEMDOS_Pwait, 0x109, ""},
    {"Pwait3",      GEMDOS_Pwait3, 0x11C, "wp"},
    {"Pwaitpid",    GEMDOS_Pwaitpid, 0x13A, "wwp"},
    {"Salert",      GEMDOS_Salert, 0x13C, "s"},
    {"Super",       GEMDOS_Super, 0x20, "p=a"},
    {"Sversion",    GEMDOS_Sversion, 0x30, "=x"},
    {"Pyield",      GEMDOS_Pyield, 0xFF, ""},
    {"Sysconf",     GEMDOS_Sysconf, 0x122, "w"},
    {"Talarm",      GEMDOS_Talarm, 0x120, "l"},
    {"Tgetdate",    GEMDOS_Tgetdate, 0x2A, "=x"},
    {"Tgettime",    GEMDOS_Tgettime, 0x2C, "=x"},
    {"Tsetdate",    GEMDOS_Tsetdate, 0x2B, "x"},
    {"Tsettime",    GEMDOS_Tsettime, 0x2D, "x"},
    {"Ssystem",     GEMDOS_Ssystem, 0x154, "wLL"},
    {"Ffstat64",    GEMDOS_Ffstat64, 0x15D, "wp"},
    {"Tgettimeofday", GEMDOS_Tgettimeofday, 0x155, "pp"},
    {"Fstat64",     GEMDOS_Fstat64, 0x14B, "wsp"},
    {"Psysctl",     GEMDOS_Psysctl, 0x15E, "pLpppL"},
    {"Fsocket",     GEMDOS_Fsocket, 0x160, "lll"},
    {"Fsocketpair", GEMDOS_Fsocketpair, 0x161, "lllp"},
    {"Faccept",     GEMDOS_Faccept, 0x162, "wpp"},
    {"Fconnect",    GEMDOS_Fconnect, 0x163, "wpl"},
    {"Fbind",       GEMDOS_Fbind, 0x164, "wpl"},
    {"Flisten",     GEMDOS_Flisten, 0x165, "wl"},
    {"Frecvmsg",    GEMDOS_Frecvmsg, 0x166, "wpl"},
    {"Fsendmsg",    GEMDOS_Fsendmsg, 0x167, "wpl"},
    {"Frecvfrom",   GEMDOS_Frecvfrom, 0x168, "wplLpp"},
    {"Fsendto",     GEMDOS_Fsendto, 0x169, "wplLpl"},
    {"Fsetsockopt", GEMDOS_Fsetsockopt, 0x16A, "wllpl"},
    {"Fgetsockopt", GEMDOS_Fgetsockopt, 0x16B, "wllpp"},
    {"Fgetpeername", GEMDOS_Fgetpeername, 0x16C, "wpp"},
    {"Fgetsockname", GEMDOS_Fgetsockname, 0x16D, "wpp"},
    {"Fshutdown",   GEMDOS_Fshutdown, 0x16E, "wl"}
};

void gemdos_init(struct tos_environment *te)
{
    /* Memory management setup */
    gemdos_mem_init(te);
    
    /* File management setup */
    gemdos_file_init(te);
}

void gemdos_reinit(struct tos_environment *te)
{
    /* The application that was running took its memory with it, and whoever
     * was waiting for it to end went with the machine they were waiting in */
    gemdos_mem_init(te);
    gemdos_proc_forget();

    /* STinG's connections were the machine's, and the machine is new */
    sting_reset();

    gemdos_file_reinit(te);
}

void gemdos_free()
{
    gemdos_mem_free();
}

void gemdos_trap()
{
    uint16_t fnct = peek_u16(0);
    int i;
    
    for(i=0; i<sizeof(GEMDOS_functions)/sizeof(struct GEMDOS_function); ++i) {
        if (GEMDOS_functions[i].id == fnct) {
            if (GEMDOS_functions[i].fnct) {
                struct GEMDOS_function *f = &GEMDOS_functions[i];
                char args[256];
                uint32_t r;

                FUNC_TRACE_ARGS {
                    calltrace_args(args, sizeof args, f->args);
                    if (calltrace_no_return(f->args))
                        calltrace_line(f->name, args, f->args, 0);
                }

                r = f->fnct();

                FUNC_TRACE_ARGS {
                    if (!calltrace_no_return(f->args))
                        calltrace_line(f->name, args, f->args, r);
                }
                m68k_set_reg(M68K_REG_D0, r);
            } else {
                halt_execution();
                printf("GEMDOS %s (0x%x) not implemented\n", GEMDOS_functions[i].name, fnct);
            }
            
            return;
        }
    }
            
    halt_execution();
    printf("GEMDOS Unknown function called 0x%x\n", fnct);
}

/* Special function implementations */
uint32_t GEMDOS_Unknown()
{
    int i;
    uint16_t fnct;
    
    FUNC_TRACE_ENTER_ARGS {
        fnct = peek_u16(0);
        printf("    func: 0x%x\n", fnct);

        for(i=0; i<sizeof(GEMDOS_functions)/sizeof(struct GEMDOS_function); ++i)
            if (GEMDOS_functions[i].id == fnct)
                if (GEMDOS_functions[i].fnct)
                    printf("    %s\n", GEMDOS_functions[i].name);        
    }

    return GEMDOS_EINVFN; /* http://toshyp.atari.org/en/005003.html */
}
