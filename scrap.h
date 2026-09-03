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

#endif /* SCRAP_H */
