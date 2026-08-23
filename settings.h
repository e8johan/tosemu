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

#ifndef SETTINGS_H
#define SETTINGS_H

/*
 * What was asked for, from the environment or from a file.
 *
 * Everything tosemu can be told is a setting, and a setting has two places it
 * can be said: an environment variable, for one run, and a line in a file, for
 * every run. The environment wins when both say something, because the point
 * of saying it there is to mean it this once.
 *
 * The names below are the environment's, because that is what the call sites
 * were already asking for and it is the shorter half of the pair. Which line
 * of which section of the file each one answers to is settings.c's business,
 * and the table there is the only place that knows both.
 */

/* Where the file is. A path names one and it is an error if it is not there; a
 * null path looks in the usual place and is content to find nothing. Call it
 * once, before anything asks for a setting, and it answers whether what was
 * asked for could be read. */
int settings_load(const char *path);

/* And to be told nothing but what the environment says, which is what a test
 * run wants: whatever is in somebody's home directory is not its business */
void settings_ignore_file(void);

/* What a setting says, or null when nothing said anything */
const char *setting(const char *name);

/* And for the ones that are a yes or a no. Anything present is a yes, except
 * the words that plainly mean no - 0, no, false, off - so that a file can turn
 * one off by saying so rather than by deleting the line. */
int setting_flag(const char *name);

/* The usual place, for saying so in a usage message */
const char *settings_default_path(void);

#endif
