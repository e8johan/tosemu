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

#ifndef AESDTRAY_H
#define AESDTRAY_H

#include <stdint.h>

/*
 * One mark in the panel, with the accessories hanging off it.
 *
 * Not one per accessory: a person does not want six mystery icons appearing
 * because a GEM program is running, they want to see that there is a session
 * and get at what is in it. It is also the only way to reach an accessory when
 * no application is running, which is the case the Desk menu cannot cover.
 *
 * Everything here is allowed to fail and none of it stops anything. Tray icons
 * are a de facto protocol rather than a standard, some desktops need an
 * extension for them and some have nothing of the sort, so a session with no
 * icon is a session that is otherwise fine.
 */

/* Says whether there is one. The answer is not worth acting on - it is said
 * plainly once and the session carries on either way. */
int tray_open(void (*when_picked)(int16_t ap_id), void (*when_quit)(void),
              void (*when_look_again)(void));
void tray_close(void);

/* Who is in the menu now */
void tray_accessories(const char *const *names, const int16_t *ap_ids, int n);

/* The connection, for the daemon to wait on beside its own socket, or -1 */
int tray_fd(void);
void tray_pump(void);

#endif /* AESDTRAY_H */
