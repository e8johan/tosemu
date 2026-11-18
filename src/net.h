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

/*
 * The host's TCP/IP, as the two Atari stacks see it - see sting.c and
 * gemdosnet.c. An emulated program's IPv4 addresses are the host's, so
 * 127.0.0.1 is the host's own loopback.
 */

#ifndef NET_H
#define NET_H

#include <stdint.h>

/* Where the built-in AERA gateway answers - see netgate.c. 192.0.2.80 is in
 * TEST-NET-1, which no real host is ever given. */
#define NET_GATEWAY_IP   (0xC0000250u)
#define NET_GATEWAY_PORT (8280)
#define NET_GATEWAY_NAME "webgate"

int net_enabled(void);

/* A non-blocking host socket of SOCK_STREAM or SOCK_DGRAM, or -errno */
int net_socket(int type);

/*
 * Starts a connection, ip and port in host order. 0 when it is made, -errno
 * otherwise, -EINPROGRESS while it is under way. The gateway's address is
 * answered by a thread of the emulator's own on the far end of a socketpair
 * that takes the place of fd.
 */
int net_connect(int fd, uint32_t ip, uint16_t port);

/* A name's IPv4 address, host order. 0 found, -1 not. */
int net_resolve(const char *name, uint32_t *ip);

/*
 * Waits for poll events on fd while the machine goes on: the interrupts are
 * serviced and STinG's output is flushed between looks. timeout_ms < 0 is for
 * ever. The revents, 0 on a timeout, -1 when the machine stopped.
 */
int net_wait(int fd, short events, long timeout_ms);

/* The same for several, which are given their revents. How many have any,
 * 0 on a timeout, -1 when the machine stopped. */
struct pollfd;
int net_wait_many(struct pollfd *fds, int n, long timeout_ms);

/* The error a socket has, as -errno, or 0 */
int net_socket_error(int fd);

#endif /* NET_H */
