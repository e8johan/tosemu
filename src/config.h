/*
 * TOSEMU - an emulated environment for TOS applications
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

#include <stdio.h>

/*
 * How much was asked for, which is one level per -v on the command line:
 *
 *   1  what the session was configured with, which is settled before the
 *      program starts and is then never mentioned again
 *   2  and every OS call it makes, which is the story of what it asked for
 *   3  and every instruction it runs, which is where it asked
 *
 * Each level is the one below it and more, because that is what somebody
 * adding a v is asking for: the same run, looked at closer.
 *
 * main.c owns it. It is declared here because the tracing below is what reads
 * it and that is in every sub-system - and because a level nobody can see the
 * meaning of is a number, not a setting.
 */
extern int verbose;

#define VERBOSE_CONFIG (1)
#define VERBOSE_CALLS  (2)
#define VERBOSE_CPU    (3)

/*
 * Which sub-systems the tracing is built into.
 *
 * All of them, because which ones say anything is now a matter of how many -v
 * were asked for rather than of how this was compiled: the whole point of a
 * level is that it can be raised on a binary somebody already has. Commenting
 * one out leaves it out of the build altogether, which is worth doing only if
 * a test and a branch on the way into an OS call ever turns out to matter -
 * and an OS call is a trap out of the emulated machine, so it will not.
 */
#define ENABLE_GEMDOS_TRACE
#define ENABLE_BIOS_TRACE
#define ENABLE_XBIOS_TRACE
#define ENABLE_AES_TRACE
#define ENABLE_VDI_TRACE
#define ENABLE_LINEA_TRACE

/* Tracing functions */

#if (defined(ENABLE_GEMDOS_TRACE) && defined(GEMDOS_TRACE_CONTEXT)) || \
    (defined(ENABLE_BIOS_TRACE) && defined(BIOS_TRACE_CONTEXT)) || \
    (defined(ENABLE_XBIOS_TRACE) && defined(XBIOS_TRACE_CONTEXT)) || \
    (defined(ENABLE_AES_TRACE) && defined(AES_TRACE_CONTEXT)) || \
    (defined(ENABLE_VDI_TRACE) && defined(VDI_TRACE_CONTEXT)) || \
    (defined(ENABLE_LINEA_TRACE) && defined(LINEA_TRACE_CONTEXT))
/* Braced, so that one of these at the top of a function cannot swallow an
 * else belonging to whatever is around it */
#define     FUNC_TRACE_ENTER      if (verbose >= VERBOSE_CALLS) \
                                  { printf("Enter %s\n", __func__); }
#define     FUNC_TRACE_ENTER_ARGS FUNC_TRACE_ENTER \
                                  if (verbose >= VERBOSE_CALLS)
/* Trace additional arguments further down a function, i.e. without repeating
 * the "Enter" line */
#define     FUNC_TRACE_ARGS       if (verbose >= VERBOSE_CALLS)
#else
#define     FUNC_TRACE_ENTER
#define     FUNC_TRACE_ENTER_ARGS if(0)
#define     FUNC_TRACE_ARGS       if(0)
#endif
