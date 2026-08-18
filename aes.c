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
 * The AES.
 *
 * d1 points at six pointers, to the control, global, intin, intout, addrin and
 * addrout arrays. The first five words of the control array say which function
 * is wanted and how long the argument arrays are:
 *
 *   control[0]  function number
 *   control[1]  entries in intin
 *   control[2]  entries in intout
 *   control[3]  entries in addrin
 *   control[4]  entries in addrout
 *
 * The counts come from the application, not from a table here, because that is
 * where they come from on a real machine: the binding fills them in from its
 * own idea of the function, and bindings disagree in places. Trusting the
 * caller and range checking every read against what it said is both simpler
 * and closer to the truth than keeping a second table to argue with it.
 *
 * A handler returns the value that belongs in intout[0], which is what every
 * AES binding reads back as the return value of the call. Handlers that answer
 * with more than one word write the rest themselves.
 */

#include <stdio.h>

#include "gem_p.h"
#include "aes_p.h"
#include "tossystem.h"
#include "m68k.h"

/* The parameter block of the call being served.
 *
 * A trap is served to completion before the next one starts, so one of these
 * is enough, in the same way that the peek functions read the one stack there
 * is.
 */
static struct {
    uint32_t control;
    uint32_t global;
    uint32_t intin;
    uint32_t intout;
    uint32_t addrin;
    uint32_t addrout;

    int16_t opcode;
    int n_intin;
    int n_intout;
    int n_addrin;
    int n_addrout;
} pb;

/* The control array is five words long by definition, so a handler asking for
 * one of them is checked against that rather than against a declared count */
#define AES_CONTROL_WORDS (5)

int16_t aes_control(int index)
{
    return gem_word(pb.control, AES_CONTROL_WORDS, index);
}

int16_t aes_intin(int index)
{
    return gem_word(pb.intin, pb.n_intin, index);
}

uint32_t aes_addrin(int index)
{
    return gem_long(pb.addrin, pb.n_addrin, index);
}

void aes_set_intout(int index, int16_t value)
{
    gem_set_word(pb.intout, pb.n_intout, index, value);
}

void aes_set_addrout(int index, uint32_t value)
{
    gem_set_long(pb.addrout, pb.n_addrout, index, value);
}

uint32_t aes_global()
{
    return pb.global;
}

/* The call table *********************************************************/

/* Functions that are named but not implemented. Naming one costs nothing and
 * turns "unknown function 0x2a" into "objc_draw is not implemented", which
 * says what an application was trying to do rather than only that it failed.
 */
#define AES_appl_read       NULL
#define AES_appl_find       NULL
#define AES_appl_tplay      NULL
#define AES_appl_trecord    NULL
#define AES_appl_bvset      NULL
#define AES_appl_yield      NULL
#define AES_appl_search     NULL
#define AES_appl_getinfo    NULL
#define AES_evnt_keybd      NULL
#define AES_evnt_button     NULL
#define AES_evnt_mouse      NULL
#define AES_evnt_dclick     NULL
#define AES_menu_text       NULL
#define AES_menu_register   NULL
#define AES_menu_popup      NULL
#define AES_menu_attach     NULL
#define AES_menu_istart     NULL
#define AES_menu_settings   NULL
#define AES_objc_add        NULL
#define AES_objc_delete     NULL
#define AES_objc_find       NULL
#define AES_objc_offset     NULL
#define AES_objc_order      NULL
#define AES_objc_edit       NULL
#define AES_objc_change     NULL
#define AES_objc_sysvar     NULL
#define AES_form_error      NULL
#define AES_form_keybd      NULL
#define AES_form_button     NULL
#define AES_graf_rubberbox  NULL
#define AES_graf_dragbox    NULL
#define AES_graf_movebox    NULL
#define AES_graf_growbox    NULL
#define AES_graf_shrinkbox  NULL
#define AES_graf_watchbox   NULL
#define AES_graf_slidebox   NULL
#define AES_graf_mkstate    NULL
#define AES_scrp_read       NULL
#define AES_scrp_write      NULL
#define AES_fsel_input      NULL
#define AES_fsel_exinput    NULL
#define AES_wind_find       NULL
#define AES_wind_new        NULL
#define AES_rsrc_rcfix      NULL
#define AES_shel_read       NULL
#define AES_shel_write      NULL
#define AES_shel_get        NULL
#define AES_shel_put        NULL
#define AES_shel_find       NULL
#define AES_shel_envrn      NULL
#define AES_shel_rdef       NULL
#define AES_shel_wdef       NULL

struct AES_function {
    char *name;
    uint32_t (*fnct)();
    uint16_t id;
    uint8_t kind;
    int32_t ret;
};

/*
 * The AES functions, http://toshyp.atari.org/en/aes.html
 *
 * The gaps in the numbering are real: the AES never defined 27 to 29, 49,
 * 57 to 69, 82 to 89, 92 to 99, or 116 to 119, and a call arriving on one of
 * them is a confused application rather than a function missing here.
 */
static struct AES_function AES_functions[] = {
    {"appl_init",      AES_appl_init,      10, FN_HALT, 0},
    {"appl_read",      AES_appl_read,      11, FN_HALT, 0},
    {"appl_write",     AES_appl_write,     12, FN_HALT, 0},
    {"appl_find",      AES_appl_find,      13, FN_HALT, 0},
    {"appl_tplay",     AES_appl_tplay,     14, FN_HALT, 0},
    {"appl_trecord",   AES_appl_trecord,   15, FN_HALT, 0},
    {"appl_bvset",     AES_appl_bvset,     16, FN_HALT, 0},
    /* Yielding is what an application does to let the others run. There is
     * only one, so it has already had its turn. */
    {"appl_yield",     AES_appl_yield,     17, FN_STUB, AES_E_OK},
    {"appl_search",    AES_appl_search,    18, FN_HALT, 0},
    {"appl_exit",      AES_appl_exit,      19, FN_HALT, 0},

    {"evnt_keybd",     AES_evnt_keybd,     20, FN_HALT, 0},
    {"evnt_button",    AES_evnt_button,    21, FN_HALT, 0},
    {"evnt_mouse",     AES_evnt_mouse,     22, FN_HALT, 0},
    {"evnt_mesag",     AES_evnt_mesag,     23, FN_HALT, 0},
    {"evnt_timer",     AES_evnt_timer,     24, FN_HALT, 0},
    {"evnt_multi",     AES_evnt_multi,     25, FN_HALT, 0},
    {"evnt_dclick",    AES_evnt_dclick,    26, FN_HALT, 0},

    {"menu_bar",       AES_menu_bar,       30, FN_HALT, 0},
    {"menu_icheck",    AES_menu_icheck,    31, FN_HALT, 0},
    {"menu_ienable",   AES_menu_ienable,   32, FN_HALT, 0},
    {"menu_tnormal",   AES_menu_tnormal,   33, FN_HALT, 0},
    {"menu_text",      AES_menu_text,      34, FN_HALT, 0},
    {"menu_register",  AES_menu_register,  35, FN_HALT, 0},
    {"menu_popup",     AES_menu_popup,     36, FN_HALT, 0},
    {"menu_attach",    AES_menu_attach,    37, FN_HALT, 0},
    {"menu_istart",    AES_menu_istart,    38, FN_HALT, 0},
    {"menu_settings",  AES_menu_settings,  39, FN_HALT, 0},

    {"objc_add",       AES_objc_add,       40, FN_HALT, 0},
    {"objc_delete",    AES_objc_delete,    41, FN_HALT, 0},
    {"objc_draw",      AES_objc_draw,      42, FN_HALT, 0},
    {"objc_find",      AES_objc_find,      43, FN_HALT, 0},
    {"objc_offset",    AES_objc_offset,    44, FN_HALT, 0},
    {"objc_order",     AES_objc_order,     45, FN_HALT, 0},
    {"objc_edit",      AES_objc_edit,      46, FN_HALT, 0},
    {"objc_change",    AES_objc_change,    47, FN_HALT, 0},
    {"objc_sysvar",    AES_objc_sysvar,    48, FN_HALT, 0},

    {"form_do",        AES_form_do,        50, FN_HALT, 0},
    {"form_dial",      AES_form_dial,      51, FN_HALT, 0},
    {"form_alert",     AES_form_alert,     52, FN_HALT, 0},
    {"form_error",     AES_form_error,     53, FN_HALT, 0},
    {"form_center",    AES_form_center,    54, FN_HALT, 0},
    {"form_keybd",     AES_form_keybd,     55, FN_HALT, 0},
    {"form_button",    AES_form_button,    56, FN_HALT, 0},

    {"graf_rubberbox", AES_graf_rubberbox, 70, FN_HALT, 0},
    {"graf_dragbox",   AES_graf_dragbox,   71, FN_HALT, 0},
    {"graf_movebox",   AES_graf_movebox,   72, FN_HALT, 0},
    {"graf_growbox",   AES_graf_growbox,   73, FN_HALT, 0},
    {"graf_shrinkbox", AES_graf_shrinkbox, 74, FN_HALT, 0},
    {"graf_watchbox",  AES_graf_watchbox,  75, FN_HALT, 0},
    {"graf_slidebox",  AES_graf_slidebox,  76, FN_HALT, 0},
    {"graf_handle",    AES_graf_handle,    77, FN_HALT, 0},
    {"graf_mouse",     AES_graf_mouse,     78, FN_HALT, 0},
    {"graf_mkstate",   AES_graf_mkstate,   79, FN_HALT, 0},

    {"scrp_read",      AES_scrp_read,      80, FN_HALT, 0},
    {"scrp_write",     AES_scrp_write,     81, FN_HALT, 0},

    {"fsel_input",     AES_fsel_input,     90, FN_HALT, 0},
    {"fsel_exinput",   AES_fsel_exinput,   91, FN_HALT, 0},

    {"wind_create",    AES_wind_create,   100, FN_HALT, 0},
    {"wind_open",      AES_wind_open,     101, FN_HALT, 0},
    {"wind_close",     AES_wind_close,    102, FN_HALT, 0},
    {"wind_delete",    AES_wind_delete,   103, FN_HALT, 0},
    {"wind_get",       AES_wind_get,      104, FN_HALT, 0},
    {"wind_set",       AES_wind_set,      105, FN_HALT, 0},
    {"wind_find",      AES_wind_find,     106, FN_HALT, 0},
    {"wind_update",    AES_wind_update,   107, FN_HALT, 0},
    {"wind_calc",      AES_wind_calc,     108, FN_HALT, 0},
    {"wind_new",       AES_wind_new,      109, FN_HALT, 0},

    {"rsrc_load",      AES_rsrc_load,     110, FN_HALT, 0},
    {"rsrc_free",      AES_rsrc_free,     111, FN_HALT, 0},
    {"rsrc_gaddr",     AES_rsrc_gaddr,    112, FN_HALT, 0},
    {"rsrc_saddr",     AES_rsrc_saddr,    113, FN_HALT, 0},
    {"rsrc_obfix",     AES_rsrc_obfix,    114, FN_HALT, 0},
    {"rsrc_rcfix",     AES_rsrc_rcfix,    115, FN_HALT, 0},

    {"shel_read",      AES_shel_read,     120, FN_HALT, 0},
    {"shel_write",     AES_shel_write,    121, FN_HALT, 0},
    {"shel_get",       AES_shel_get,      122, FN_HALT, 0},
    {"shel_put",       AES_shel_put,      123, FN_HALT, 0},
    {"shel_find",      AES_shel_find,     124, FN_HALT, 0},
    {"shel_envrn",     AES_shel_envrn,    125, FN_HALT, 0},
    {"shel_rdef",      AES_shel_rdef,     126, FN_HALT, 0},
    {"shel_wdef",      AES_shel_wdef,     127, FN_HALT, 0},

    {"appl_getinfo",   AES_appl_getinfo,  130, FN_HALT, 0}
};

void aes_reset()
{
    aes_appl_reset();
}

void aes_trap()
{
    uint32_t block = m68k_get_reg(0, M68K_REG_D1);
    int i;

    pb.control = m68k_read_memory_32(block +  0);
    pb.global  = m68k_read_memory_32(block +  4);
    pb.intin   = m68k_read_memory_32(block +  8);
    pb.intout  = m68k_read_memory_32(block + 12);
    pb.addrin  = m68k_read_memory_32(block + 16);
    pb.addrout = m68k_read_memory_32(block + 20);

    pb.opcode    = gem_word(pb.control, AES_CONTROL_WORDS, 0);
    pb.n_intin   = gem_word(pb.control, AES_CONTROL_WORDS, 1);
    pb.n_intout  = gem_word(pb.control, AES_CONTROL_WORDS, 2);
    pb.n_addrin  = gem_word(pb.control, AES_CONTROL_WORDS, 3);
    pb.n_addrout = gem_word(pb.control, AES_CONTROL_WORDS, 4);

#ifdef ENABLE_AES_TRACE
    printf("AES call %d: intin %d, intout %d, addrin %d, addrout %d\n",
           pb.opcode, pb.n_intin, pb.n_intout, pb.n_addrin, pb.n_addrout);
#endif

    for (i = 0; i < (int)(sizeof(AES_functions)/sizeof(struct AES_function)); ++i)
    {
        struct AES_function *f = &AES_functions[i];
        uint32_t r;

        if (f->id != pb.opcode)
            continue;

        if (f->fnct)
        {
            r = f->fnct();
        }
        else if (f->kind == FN_STUB)
        {
            r = f->ret;
#ifdef ENABLE_AES_TRACE
            printf("Stubbed %s (%d)\n", f->name, pb.opcode);
#endif
        }
        else
        {
            halt_execution();
            printf("AES %s (%d) not implemented\n", f->name, pb.opcode);
            return;
        }

#ifdef ENABLE_AES_TRACE
        printf("Return from %s: %d = 0x%x\n", f->name, r, r);
#endif

        /* Every AES binding reads the return value out of intout[0], and the
         * AES leaves it in d0 as well. A function that declared no intout has
         * nothing to say, so it is not given somewhere to say it. */
        if (pb.n_intout > 0)
            aes_set_intout(0, (int16_t)r);
        m68k_set_reg(M68K_REG_D0, r);

        return;
    }

    halt_execution();
    printf("AES Unknown function called %d\n", pb.opcode);
}
