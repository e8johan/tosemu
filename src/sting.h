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
 * STinG's transport layer, the TCP/IP a TOS program finds through the STiK
 * cookie, answered by the host's sockets.
 */

#ifndef STING_H
#define STING_H

/* Puts the driver list, the transport layer and the STiK cookie into a
 * freshly built machine. Nothing when the network is turned off. */
void sting_init(void);

/* Closes every connection: STinG is resident, so they outlive the program
 * that opened them until the machine itself goes */
void sting_reset(void);

/* Sends what is waiting in the connections' output buffers */
void sting_flush(void);

/* A line-A opcode in STinG's range, called from musashi.c. Whether it was. */
int sting_linea(unsigned int opcode);

#endif /* STING_H */
