/*
 * TOSEMU - an emulated environment for TOS applications
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

#ifndef SCRAP_H
#define SCRAP_H

#include <stddef.h>

/*
 * The scrap, and the desktop's clipboard, which are the same idea kept in two
 * incompatible ways.
 *
 * GEM's is a directory. An application that cuts something out writes a file
 * called SCRAP into it with an extension saying what kind of thing it is, and
 * one that pastes looks for the kinds it can read. Nothing is held in memory
 * and nothing passes between programs directly - see aesscrp.c.
 *
 * A desktop's is a selection: one client says it has something and what forms
 * it can produce, and hands the bytes over when somebody asks. Nothing is
 * written down at all.
 *
 * Bridging them is two separate jobs that share only this file. Going out, a
 * GEM program never announces that it copied - so a file appearing in the
 * directory is the only signal there is, and it has to be watched for. Coming
 * in, the desktop announces constantly and writes nothing, so what it offers
 * is remembered and turned into a file at the moment GEM looks for one.
 */

/*
 * Where the scrap is, as an application spells it. Answers the session's, or
 * settles on a default and tells the session about it when nothing has said
 * yet - which is what lets a program paste from the desktop before any GEM
 * program has copied anything.
 */
void scrap_where(char *tos_path, size_t size);

/* Says where it is now, which is scrp_write arriving */
void scrap_watch(const char *tos_path);

/*
 * Puts what the desktop is offering into the scrap directory, if it is offering
 * anything newer than what is there already.
 *
 * Call it wherever GEM is about to look at the scrap. It is cheap when there is
 * nothing to do, which is almost always.
 */
void scrap_refresh(void);

/* The same, for a GEMDOS call about to read a host path: does nothing unless
 * that path is in the scrap directory */
void scrap_refresh_for(const char *host_path);

/*
 * The watch on the scrap directory, for the event loop to wait on beside the
 * compositor and the daemon, or -1 when there is nothing being watched.
 *
 * It is a watch rather than a call because a GEM application never says it
 * copied anything. scrp_write says only which directory the scrap is in; the
 * cut itself is a file appearing in it, some time later and with nothing to
 * announce it, so noticing the file is the only signal there is.
 *
 * Nothing that arrives here can end a wait the application is in. It is the
 * desktop that is being served, not the program - see the note in aesevnt.c
 * about why that distinction has to be kept in the poll set.
 */
int scrap_fd(void);

/* Something happened in the scrap directory: reads what, and offers whatever
 * was cut to the desktop */
void scrap_pump(void);

#endif /* SCRAP_H */
