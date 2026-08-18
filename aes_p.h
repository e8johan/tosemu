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

/* The frame round a window, aesframe.c */

void aes_frame_draw(int16_t kind, int16_t x, int16_t y, int16_t w, int16_t h,
                    int16_t hslide, int16_t hslsize,
                    int16_t vslide, int16_t vslsize);

/* The file selector, aesfsel.c */

uint32_t AES_fsel_input();
uint32_t AES_fsel_exinput();

/* The scrap, aesscrp.c */

uint32_t AES_scrp_read();
uint32_t AES_scrp_write();

/* The shell, aesshel.c */

uint32_t AES_shel_write();
uint32_t AES_shel_read();

/* Resource files, aesrsrc.c */

void aes_rsrc_reset();

uint32_t AES_rsrc_load();
uint32_t AES_rsrc_free();
uint32_t AES_rsrc_gaddr();
uint32_t AES_rsrc_saddr();
uint32_t AES_rsrc_obfix();

/* Application functions, aesappl.c */

void aes_appl_reset();

uint32_t AES_appl_init();
uint32_t AES_appl_write();
uint32_t AES_appl_exit();
uint32_t AES_appl_find();
void aes_appl_finished();

/* Event functions, aesevnt.c */

void aes_evnt_reset();

/* Puts a message where the application waiting on evnt_mesag will find it.
 * Returns 0 when the queue is full. */
int aes_message_post(const int16_t *message);

uint32_t AES_evnt_timer();
uint32_t AES_evnt_mesag();
uint32_t AES_evnt_multi();
uint32_t AES_evnt_keybd();
uint32_t AES_evnt_button();
uint32_t AES_evnt_mouse();
uint32_t AES_evnt_dclick();

/* Object trees, aestree.c
 *
 * A tree lives in the machine's memory, where the AES cannot read it: the
 * words are the other way round and the pointers in it are 68000 addresses.
 * These bring one across and put back the parts the AES is allowed to have
 * changed.
 */
void *aes_tree_in(uint32_t address);
void aes_tree_out();
void aes_tree_done();

/* Object functions, aesobjc.c */

uint32_t AES_objc_draw();
uint32_t AES_objc_add();
uint32_t AES_objc_delete();
uint32_t AES_objc_find();
uint32_t AES_objc_offset();
uint32_t AES_objc_order();
uint32_t AES_objc_change();
uint32_t AES_objc_edit();
uint32_t AES_form_do();
uint32_t AES_form_dial();
uint32_t AES_form_center();
uint32_t AES_form_keybd();
uint32_t AES_form_button();
uint32_t AES_form_error();
uint32_t AES_form_alert();

/* Menu functions, aesmenu.c */

void aes_menu_reset();

/* Whether a point is in the menu bar, and running the menu when one is */
int aes_menu_shown();
int aes_menu_arrived(int16_t x, int16_t y);
void aes_menu_click();

uint32_t AES_menu_bar();
uint32_t AES_menu_icheck();
uint32_t AES_menu_ienable();
uint32_t AES_menu_tnormal();
uint32_t AES_menu_text();

/* Window functions, aeswind.c */

void aes_wind_reset();

uint32_t AES_wind_create();
uint32_t AES_wind_open();
uint32_t AES_wind_close();
uint32_t AES_wind_delete();
uint32_t AES_wind_get();
uint32_t AES_wind_set();
uint32_t AES_wind_calc();
uint32_t AES_wind_update();
uint32_t AES_wind_find();
uint32_t AES_wind_new();

/* Graphics functions, aesgraf.c */

uint32_t AES_graf_handle();
uint32_t AES_graf_mouse();
uint32_t AES_graf_rubberbox();
uint32_t AES_graf_dragbox();
uint32_t AES_graf_movebox();
uint32_t AES_graf_growbox();
uint32_t AES_graf_shrinkbox();
uint32_t AES_graf_watchbox();
uint32_t AES_graf_slidebox();
uint32_t AES_graf_mkstate();

#endif /* AES_P_H */
