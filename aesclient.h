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

#ifndef AESCLIENT_H
#define AESCLIENT_H

#include <stddef.h>
#include <stdint.h>

/*
 * The emulator's end of the conversation with the daemon.
 *
 * There does not have to be a daemon. One application on its own is the
 * ordinary case and has nobody to talk to, so everything here answers as it
 * would for an application that is the only one there is, and the emulator
 * runs exactly as it did before there was a daemon at all. That is not a
 * fallback bolted on: it is the same answer, arrived at without asking.
 */

/* Connects, if there is one to connect to. Says whether there is. */
int aes_client_open(void);
void aes_client_close(void);

/* Lets go of a connection this process inherited by being forked, without
 * telling the daemon that the application it belongs to has gone */
void aes_client_forget(void);

/* Whether a daemon is answering */
int aes_client_connected(void);

/*
 * Says an application has started and answers with the identifier it was
 * given. Without a daemon that is nought, because it is the first application
 * there is.
 */
int16_t aes_client_hello(const char *name);

/* How many applications can run at once, and how large the screen they share
 * is. Without a daemon, one, and the size this build was made with. */
int16_t aes_client_apps(void);
void aes_client_screen(int16_t *width, int16_t *height, int16_t *planes);

/* Which application answers to a name, or -1 for none. Without a daemon there
 * is only this one, and it is not looking for itself. */
int16_t aes_client_find(const char *name);

/* Sends eight words to another application. Answers 0 when there is nobody to
 * send to, which is what appl_write reports as a failure. */
int aes_client_send(int16_t to, const int16_t *message);

/*
 * Where the scrap is: the directory two applications both write their cut and
 * pasted files into. The only part they have to agree about is which directory,
 * so it is the only part that crosses the seam.
 */
void aes_client_scrap_get(char *path, size_t size);
void aes_client_scrap_set(const char *path);

/*
 * Accessories.
 *
 * An accessory is an application with no window until somebody picks it out of
 * the Desk menu, and that menu is in every application's bar - so who they are
 * has to reach all of them, which makes the list the daemon's. One says what
 * it is called and everybody finds out, including those that started first.
 */
void aes_client_accessory(const char *name);
int16_t aes_client_accessories(void);
const char *aes_client_accessory_name(int16_t which);
int16_t aes_client_accessory_owner(int16_t which);

/*
 * The connection, for the event loop to wait on beside the compositor and its
 * timer, or -1 when there is no daemon.
 */
int aes_client_fd(void);

/*
 * Takes whatever the daemon has said and puts any messages where the
 * application waiting on evnt_mesag will find them.
 */
void aes_client_pump(void);

#endif /* AESCLIENT_H */
