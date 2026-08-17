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

#ifndef AES_P_H
#define AES_P_H

#include <stdint.h>
#include <stdio.h>

#define AES_TRACE_CONTEXT
#include "config.h"

/* AES return values.
 *
 * Unlike GEMDOS, the AES has no table of error numbers. Nearly every call
 * answers in intout[0] with a value that is positive when the call did
 * something and zero when it did not, so those are the only two names worth
 * having. The calls that answer with something else, an identifier or a
 * count, return it directly.
 * http://toshyp.atari.org/en/aes.html
 */
#define AES_E_OK  (1)
#define AES_ERROR (0)

/* The AES version reported in global[0].
 *
 * 0x0140 is the AES of TOS 1.4. It is a deliberately modest claim: an
 * application reads this to decide whether the later calls exist, so
 * reporting more than is implemented moves applications onto paths that are
 * not there yet. This goes up as the rest of the AES is built, and
 * appl_getinfo, which is how an application asks in detail, stays out of
 * reach until it does.
 */
#define AES_VERSION (0x0140)

/* How many applications can be running at once. The AES of a single tasking
 * TOS answers 1, and until the daemon owns the application table that is the
 * truth here as well.
 */
#define AES_APPS (1)

/* The parameter block of the call being served.
 *
 * A handler takes no arguments, in the same way that a GEMDOS handler takes
 * none and reads the stack. These read the arrays the application passed,
 * bounds checked against the counts it declared in the control array.
 */
int16_t aes_control(int index);
int16_t aes_intin(int index);
uint32_t aes_addrin(int index);
void aes_set_intout(int index, int16_t value);
void aes_set_addrout(int index, uint32_t value);

/* The address of the global array, which is not described by the control
 * array and is written by appl_init alone */
uint32_t aes_global();

/* Application functions, aesappl.c */

void aes_appl_reset();

uint32_t AES_appl_init();
uint32_t AES_appl_exit();

#endif /* AES_P_H */
