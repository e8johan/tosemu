/*
 * TOSEMU - and emulated environment for TOS applications
 * Copyright (C) 2014 Johan Thelin <e8johan@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>

#include "tossystem.h"
#include "m68k.h"
#include "utils.h"

/* GEMDOS function table according to
 * http://www.yardley.cc/atari/compendium/atari-compendium-GEMDOS-Function-Reference.htm
 */
#define GEMDOS_Cauxin (0x03)
#define GEMDOS_Cauxis (0x12)
#define GEMDOS_Cauxos (0x13)
#define GEMDOS_Cauxout (0x04)
#define GEMDOS_Cconin (0x01)
#define GEMDOS_Cconis (0x0B)
#define GEMDOS_Cconos (0x10)
#define GEMDOS_Cconout (0x02)
#define GEMDOS_Cconrs (0x0A)
#define GEMDOS_Cconws (0x09)
#define GEMDOS_Cnecin (0x08)
#define GEMDOS_Cprnos (0x11)
#define GEMDOS_Cprnout (0x05)
#define GEMDOS_Crawcin (0x07)
#define GEMDOS_Crawio (0x06)
#define GEMDOS_Dclosedir (0x12B)
#define GEMDOS_Dcntl (0x130)
#define GEMDOS_Dcreate (0x39)
#define GEMDOS_Ddelete (0x3A)
#define GEMDOS_Dfree (0x36)
#define GEMDOS_Dgetcwd (0x13B)
#define GEMDOS_Dgetdrv (0x19)
#define GEMDOS_Dgetpath (0x47)
#define GEMDOS_Dlock (0x135)
#define GEMDOS_Dopendir (0x128)
#define GEMDOS_Dpathconf (0x124)
#define GEMDOS_Dreaddir (0x129)
#define GEMDOS_Drewinddir (0x12A)
#define GEMDOS_Dsetdrv (0x0E)
#define GEMDOS_Dsetpath (0x3B)
#define GEMDOS_Fattrib (0x43)
#define GEMDOS_Fchmod (0x132)
#define GEMDOS_Fchown (0x131)
#define GEMDOS_Fclose (0x3E)
#define GEMDOS_Fcntl (0x104)
#define GEMDOS_Fcreate (0x3C)
#define GEMDOS_Fdatime (0x57)
#define GEMDOS_Fdelete (0x41)
#define GEMDOS_Fdup (0x45)
#define GEMDOS_Fforce (0x46)
#define GEMDOS_Fgetchar (0x107)
#define GEMDOS_Fgetdta (0x2F)
#define GEMDOS_Finstat (0x105)
#define GEMDOS_Flink (0x12D)
#define GEMDOS_Flock (0x5C)
#define GEMDOS_Fmidipipe (0x126)
#define GEMDOS_Fopen (0x3D)
#define GEMDOS_Foutstat (0x106)
#define GEMDOS_Fpipe (0x100)
#define GEMDOS_Fputchar (0x108)
#define GEMDOS_Fread (0x3F)
#define GEMDOS_Freadlink (0x12F)
#define GEMDOS_Frename (0x56)
#define GEMDOS_Fseek (0x42)
#define GEMDOS_Fselect (0x11D)
#define GEMDOS_Fsetdta (0x1A)
#define GEMDOS_Fsfirst (0x4E)
#define GEMDOS_Fsnext (0x4F)
#define GEMDOS_Fsymlink (0x12E)
#define GEMDOS_Fwrite (0x40)
#define GEMDOS_Fxattr (0x12C)
#define GEMDOS_Maddalt (0x14)
#define GEMDOS_Malloc (0x48)
#define GEMDOS_Mfree (0x49)
#define GEMDOS_Mshrink (0x4A)
#define GEMDOS_Mxalloc (0x44)
#define GEMDOS_Pause (0x121)
#define GEMDOS_Pdomain (0x119)
#define GEMDOS_Pexec (0x4B)
#define GEMDOS_Pfork (0x11B)
#define GEMDOS_Pgetegid (0x139)
#define GEMDOS_Pgeteuid (0x138)
#define GEMDOS_Pgetgid (0x114)
#define GEMDOS_Pgetpgrp (0x10D)
#define GEMDOS_Pgetpid (0x10B)
#define GEMDOS_Pgetppid (0x10C)
#define GEMDOS_Pgetuid (0x10F)
#define GEMDOS_Pkill (0x111)
#define GEMDOS_Pmsg (0x125)
#define GEMDOS_Pnice (0x10A)
#define GEMDOS_Prenice (0x127)
#define GEMDOS_Prusage (0x11E)
#define GEMDOS_Psemaphore (0x134)
#define GEMDOS_Psetgit (0x115)
#define GEMDOS_Psetlimit (0x11F)
#define GEMDOS_Psetpgrp (0x10E)
#define GEMDOS_Psetuid (0x110)
#define GEMDOS_Psigaction (0x137)
#define GEMDOS_Psigblock (0x116)
#define GEMDOS_Psignal (0x112)
#define GEMDOS_Psigpause (0x136)
#define GEMDOS_Psigpending (0x123)
#define GEMDOS_Psigreturn (0x11A)
#define GEMDOS_Psigsetmask (0x117)
#define GEMDOS_Pterm (0x4C)
#define GEMDOS_Pterm0 (0x0)
#define GEMDOS_Ptermres (0x31)
#define GEMDOS_Pumask (0x133)
#define GEMDOS_Pursval (0x118)
#define GEMDOS_Pvfork (0x113)
#define GEMDOS_Pwait (0x109)
#define GEMDOS_Pwait3 (0x11C)
#define GEMDOS_Pwaitpid (0x13A)
#define GEMDOS_Salert (0x13C)
#define GEMDOS_Super (0x20)
#define GEMDOS_Sversion (0x30)
#define GEMDOS_Pyield (0xFF)
#define GEMDOS_Sysconf (0x122)
#define GEMDOS_Talarm (0x120)
#define GEMDOS_Tgetdate (0x2A)
#define GEMDOS_Tgettime (0x2C)
#define GEMDOS_Tsetdate (0x2B)
#define GEMDOS_Tsettime (0x2D)

void gemdos_trap()
{
    uint32_t sp = m68k_get_reg(0, M68K_REG_A7);
    uint16_t fnct;
    
    uint32_t lv0;
    
    fnct = endianize_16(m68k_read_disassembler_16(sp));
    
    switch(fnct)
    {
    case GEMDOS_Pterm:
        exit(endianize_16(m68k_read_disassembler_16(sp+2)));
        break;
        
    case GEMDOS_Pterm0:
        exit(0);
        break;
        
    case GEMDOS_Super:
        lv0 = endianize_32(m68k_read_disassembler_32(sp+2));
        
        if (lv0 == 0) { /* Set CPU in supervisor mode */
            m68k_set_reg(M68K_REG_D0, m68k_get_reg(0, M68K_REG_A7));
            m68k_set_reg(M68K_REG_SR, m68k_get_reg(0, M68K_REG_SR) | 0x2000); /* set the CPU in supervisor mode */
        } else if (lv0 == 1) { /* Return 1 if in supervisor mode, otherwise zero */
            if ((m68k_get_reg(0, M68K_REG_SR) & 0x2000) == 0x2000) {
                m68k_set_reg(M68K_REG_D0, 1);
            } else {
                m68k_set_reg(M68K_REG_D0, 0);
            }
        } else { /* Set CPU in user mode, set SP to lv0 */
            m68k_set_reg(M68K_REG_USP, lv0);
            m68k_set_reg(M68K_REG_SR, m68k_get_reg(0, M68K_REG_SR) & (~0x2000)); /* set the CPU in user mode */
        }
        break;
        
    case  GEMDOS_Mshrink:
        /* TODO currently, we do not react to this */
        m68k_set_reg(M68K_REG_D0, 0);
        break;
        
    case GEMDOS_Cauxin:
    case GEMDOS_Cauxis:
    case GEMDOS_Cauxos:
    case GEMDOS_Cauxout:
    case GEMDOS_Cconin:
    case GEMDOS_Cconis:
    case GEMDOS_Cconos:
    case GEMDOS_Cconout:
    case GEMDOS_Cconrs:
    case GEMDOS_Cconws:
    case GEMDOS_Cnecin:
    case GEMDOS_Cprnos:
    case GEMDOS_Cprnout:
    case GEMDOS_Crawcin:
    case GEMDOS_Crawio:
    case GEMDOS_Dclosedir:
    case GEMDOS_Dcntl:
    case GEMDOS_Dcreate:
    case GEMDOS_Ddelete:
    case GEMDOS_Dfree:
    case GEMDOS_Dgetcwd:
    case GEMDOS_Dgetdrv:
    case GEMDOS_Dgetpath:
    case GEMDOS_Dlock:
    case GEMDOS_Dopendir:
    case GEMDOS_Dpathconf:
    case GEMDOS_Dreaddir:
    case GEMDOS_Drewinddir:
    case GEMDOS_Dsetdrv:
    case GEMDOS_Dsetpath:
    case GEMDOS_Fattrib:
    case GEMDOS_Fchmod:
    case GEMDOS_Fchown:
    case GEMDOS_Fclose:
    case GEMDOS_Fcntl:
    case GEMDOS_Fcreate:
    case GEMDOS_Fdatime:
    case GEMDOS_Fdelete:
    case GEMDOS_Fdup:
    case GEMDOS_Fforce:
    case GEMDOS_Fgetchar:
    case GEMDOS_Fgetdta:
    case GEMDOS_Finstat:
    case GEMDOS_Flink:
    case GEMDOS_Flock:
    case GEMDOS_Fmidipipe:
    case GEMDOS_Fopen:
    case GEMDOS_Foutstat:
    case GEMDOS_Fpipe:
    case GEMDOS_Fputchar:
    case GEMDOS_Fread:
    case GEMDOS_Freadlink:
    case GEMDOS_Frename:
    case GEMDOS_Fseek:
    case GEMDOS_Fselect:
    case GEMDOS_Fsetdta:
    case GEMDOS_Fsfirst:
    case GEMDOS_Fsnext:
    case GEMDOS_Fsymlink:
    case GEMDOS_Fwrite:
    case GEMDOS_Fxattr:
    case GEMDOS_Maddalt:
    case GEMDOS_Malloc:
    case GEMDOS_Mfree:
    case GEMDOS_Mxalloc:
    case GEMDOS_Pause:
    case GEMDOS_Pdomain:
    case GEMDOS_Pexec:
    case GEMDOS_Pfork:
    case GEMDOS_Pgetegid:
    case GEMDOS_Pgeteuid:
    case GEMDOS_Pgetgid:
    case GEMDOS_Pgetpgrp:
    case GEMDOS_Pgetpid:
    case GEMDOS_Pgetppid:
    case GEMDOS_Pgetuid:
    case GEMDOS_Pkill:
    case GEMDOS_Pmsg:
    case GEMDOS_Pnice:
    case GEMDOS_Prenice:
    case GEMDOS_Prusage:
    case GEMDOS_Psemaphore:
    case GEMDOS_Psetgit:
    case GEMDOS_Psetlimit:
    case GEMDOS_Psetpgrp:
    case GEMDOS_Psetuid:
    case GEMDOS_Psigaction:
    case GEMDOS_Psigblock:
    case GEMDOS_Psignal:
    case GEMDOS_Psigpause:
    case GEMDOS_Psigpending:
    case GEMDOS_Psigreturn:
    case GEMDOS_Psigsetmask:
    case GEMDOS_Ptermres:
    case GEMDOS_Pumask:
    case GEMDOS_Pursval:
    case GEMDOS_Pvfork:
    case GEMDOS_Pwait:
    case GEMDOS_Pwait3:
    case GEMDOS_Pwaitpid:
    case GEMDOS_Salert:
    case GEMDOS_Sversion:
    case GEMDOS_Pyield:
    case GEMDOS_Sysconf:
    case GEMDOS_Talarm:
    case GEMDOS_Tgetdate:
    case GEMDOS_Tgettime:
    case GEMDOS_Tsetdate:
    case GEMDOS_Tsettime:
        halt_execution();
        printf("GEMDOS unimplemented 0x%x\n", fnct);
        break;
    default:
        halt_execution();
        printf("GEMDOS: Unknown function called 0x%x\n", fnct);
        break;
    }
}
