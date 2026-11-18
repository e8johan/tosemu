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

#ifndef GEMDOSNET_P_H
#define GEMDOSNET_P_H

#include <stdint.h>

/* MiNTnet's socket calls, over the host's sockets - see gemdosnet.c */
uint32_t GEMDOS_Fsocket();
uint32_t GEMDOS_Faccept();
uint32_t GEMDOS_Fconnect();
uint32_t GEMDOS_Fbind();
uint32_t GEMDOS_Flisten();
uint32_t GEMDOS_Frecvfrom();
uint32_t GEMDOS_Fsendto();
uint32_t GEMDOS_Fsetsockopt();
uint32_t GEMDOS_Fgetsockopt();
uint32_t GEMDOS_Fgetpeername();
uint32_t GEMDOS_Fgetsockname();
uint32_t GEMDOS_Fshutdown();
uint32_t GEMDOS_Fcntl();
uint32_t GEMDOS_Fselect();

/* Fread and Fwrite on a handle that is a socket, sflags being what Fcntl set */
int32_t gemdos_socket_read(int fd, uint32_t sflags, uint32_t buf, uint32_t len);
int32_t gemdos_socket_write(int fd, uint32_t sflags, uint32_t buf, uint32_t len);

#endif /* GEMDOSNET_P_H */
