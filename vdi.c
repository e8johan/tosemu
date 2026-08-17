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
 * The VDI.
 *
 * d1 points at five pointers, to the control, intin, ptsin, intout and ptsout
 * arrays. Coordinates travel in their own arrays rather than mixed in with the
 * other arguments, which is why there are two of each. The control array says
 * what is wanted:
 *
 *   control[0]  function number
 *   control[1]  entries in ptsin
 *   control[2]  entries in ptsout, filled in by the VDI
 *   control[3]  entries in intin
 *   control[4]  entries in intout, filled in by the VDI
 *   control[5]  sub function, for the opcodes that have one
 *   control[6]  the workstation handle
 *
 * Two opcodes are really families. Opcode 11 is the generalised drawing
 * primitives, where control[5] chooses between a bar, an arc, a rounded box
 * and the rest, and opcode 5 is the escapes, which is where everything that
 * did not fit anywhere else ended up. Both are dispatched a second time by
 * their sub function.
 *
 * A handler returns a value only so that one dispatcher shape serves all four
 * of the emulated subsystems. The VDI answers through its arrays, and no
 * binding reads d0 after a VDI trap, so nothing is done with the return beyond
 * reporting it in the trace.
 */

#include <stdio.h>

#include "gem_p.h"
#include "vdi_p.h"
#include "tossystem.h"
#include "m68k.h"

static struct {
    uint32_t control;
    uint32_t intin;
    uint32_t ptsin;
    uint32_t intout;
    uint32_t ptsout;

    int16_t opcode;
    int n_ptsin;
    int n_intin;

    /* How much room the caller left to answer in. The VDI has no counts for
     * these on the way in, so the arrays are trusted as far as any documented
     * function reaches into them: NVDI 4 puts the ceiling at 512 words of
     * intout and 256 points of ptsout. */
    int n_intout;
    int n_ptsout;
} pb;

/* The ceilings NVDI 4 documents for the parameter block arrays. The control
 * array is the only one whose length is fixed rather than declared, and the
 * later calls reach further into it than the original eleven words. */
#define VDI_CONTROL_WORDS (15)
#define VDI_INTOUT_MAX   (512)
#define VDI_PTSOUT_MAX   (256)

int16_t vdi_control(int index)
{
    return gem_word(pb.control, VDI_CONTROL_WORDS, index);
}

int16_t vdi_intin(int index)
{
    return gem_word(pb.intin, pb.n_intin, index);
}

int16_t vdi_ptsin(int index)
{
    return gem_word(pb.ptsin, pb.n_ptsin, index);
}

void vdi_set_intout(int index, int16_t value)
{
    gem_set_word(pb.intout, pb.n_intout, index, value);
}

void vdi_set_ptsout(int index, int16_t value)
{
    gem_set_word(pb.ptsout, pb.n_ptsout, index, value);
}

void vdi_set_intout_count(int words)
{
    gem_set_word(pb.control, VDI_CONTROL_WORDS, 4, (int16_t)words);
}

void vdi_set_ptsout_count(int words)
{
    /* Both counts are given in the same units the set functions index in,
     * which is words. control[2] is the one place that differs: it counts the
     * points themselves, and a point is an x and a y. */
    gem_set_word(pb.control, VDI_CONTROL_WORDS, 2, (int16_t)(words/2));
}

int16_t vdi_handle()
{
    return vdi_control(6);
}

int16_t vdi_subfunction()
{
    return vdi_control(5);
}

/* The call table *********************************************************/

#define VDI_v_opnwk           NULL
#define VDI_v_clswk           NULL
#define VDI_v_clrwk           NULL
#define VDI_v_updwk           NULL
#define VDI_escape            NULL
#define VDI_v_pline           NULL
#define VDI_v_pmarker         NULL
#define VDI_v_gtext           NULL
#define VDI_v_fillarea        NULL
#define VDI_v_cellarray       NULL
#define VDI_gdp               NULL
#define VDI_vst_height        NULL
#define VDI_vst_rotation      NULL
#define VDI_vs_color          NULL
#define VDI_vsl_type          NULL
#define VDI_vsl_width         NULL
#define VDI_vsl_color         NULL
#define VDI_vsm_type          NULL
#define VDI_vsm_height        NULL
#define VDI_vsm_color         NULL
#define VDI_vst_font          NULL
#define VDI_vst_color         NULL
#define VDI_vsf_interior      NULL
#define VDI_vsf_style         NULL
#define VDI_vsf_color         NULL
#define VDI_vq_color          NULL
#define VDI_vq_cellarray      NULL
#define VDI_v_locator         NULL
#define VDI_v_valuator        NULL
#define VDI_v_choice          NULL
#define VDI_v_string          NULL
#define VDI_vswr_mode         NULL
#define VDI_vsin_mode         NULL
#define VDI_vql_attributes    NULL
#define VDI_vqm_attributes    NULL
#define VDI_vqf_attributes    NULL
#define VDI_vqt_attributes    NULL
#define VDI_vst_alignment     NULL
#define VDI_v_opnvwk          NULL
#define VDI_v_clsvwk          NULL
#define VDI_vq_extnd          NULL
#define VDI_v_contourfill     NULL
#define VDI_vsf_perimeter     NULL
#define VDI_v_get_pixel       NULL
#define VDI_vst_effects       NULL
#define VDI_vst_point         NULL
#define VDI_vsl_ends          NULL
#define VDI_vro_cpyfm         NULL
#define VDI_vr_trnfm          NULL
#define VDI_vsc_form          NULL
#define VDI_vsf_updat         NULL
#define VDI_vsl_udsty         NULL
#define VDI_vr_recfl          NULL
#define VDI_vqin_mode         NULL
#define VDI_vqt_extent        NULL
#define VDI_vqt_width         NULL
#define VDI_vex_timv          NULL
#define VDI_vst_load_fonts    NULL
#define VDI_vst_unload_fonts  NULL
#define VDI_vrt_cpyfm         NULL
#define VDI_v_show_c          NULL
#define VDI_v_hide_c          NULL
#define VDI_vq_mouse          NULL
#define VDI_vex_butv          NULL
#define VDI_vex_motv          NULL
#define VDI_vex_curv          NULL
#define VDI_vq_key_s          NULL
#define VDI_vs_clip           NULL
#define VDI_vqt_name          NULL
#define VDI_vqt_fontinfo      NULL

/* The Speedo and FontGDOS additions, which arrive on their own range of
 * function numbers well above the VDI proper */
#define VDI_vqt_fontheader    NULL
#define VDI_vqt_trackkern     NULL
#define VDI_vqt_pairkern      NULL
#define VDI_vst_charmap       NULL
#define VDI_vst_kern          NULL
#define VDI_v_getbitmap_info  NULL
#define VDI_vqt_f_extent      NULL
#define VDI_v_ftext           NULL
#define VDI_v_ftext_offset    NULL
#define VDI_v_getoutline      NULL
#define VDI_vst_scratch       NULL
#define VDI_vst_error         NULL
#define VDI_vst_arbpt         NULL
#define VDI_vqt_advance       NULL
#define VDI_vqt_devinfo       NULL
#define VDI_v_savecache       NULL
#define VDI_v_loadcache       NULL
#define VDI_v_flushcache      NULL
#define VDI_vst_setsize       NULL
#define VDI_vst_skew          NULL
#define VDI_vqt_get_table     NULL
#define VDI_vqt_cachesize     NULL

struct VDI_function {
    char *name;
    uint32_t (*fnct)();
    uint16_t id;
    uint8_t kind;
};

/*
 * The VDI functions, http://toshyp.atari.org/en/vdi.html
 *
 * The input functions come in a requesting and a sampling form which share a
 * function number and are told apart by the input mode, so one row covers
 * both: vrq_locator and vsm_locator are the same call asked twice.
 */
static struct VDI_function VDI_functions[] = {
    {"v_opnwk",          VDI_v_opnwk,           1, FN_HALT},
    {"v_clswk",          VDI_v_clswk,           2, FN_HALT},
    {"v_clrwk",          VDI_v_clrwk,           3, FN_HALT},
    /* Nothing here holds drawing back waiting to be flushed */
    {"v_updwk",          VDI_v_updwk,           4, FN_STUB},
    {"escape",           VDI_escape,            5, FN_HALT},
    {"v_pline",          VDI_v_pline,           6, FN_HALT},
    {"v_pmarker",        VDI_v_pmarker,         7, FN_HALT},
    {"v_gtext",          VDI_v_gtext,           8, FN_HALT},
    {"v_fillarea",       VDI_v_fillarea,        9, FN_HALT},
    {"v_cellarray",      VDI_v_cellarray,      10, FN_HALT},
    {"gdp",              VDI_gdp,              11, FN_HALT},
    {"vst_height",       VDI_vst_height,       12, FN_HALT},
    {"vst_rotation",     VDI_vst_rotation,     13, FN_HALT},
    {"vs_color",         VDI_vs_color,         14, FN_HALT},
    {"vsl_type",         VDI_vsl_type,         15, FN_HALT},
    {"vsl_width",        VDI_vsl_width,        16, FN_HALT},
    {"vsl_color",        VDI_vsl_color,        17, FN_HALT},
    {"vsm_type",         VDI_vsm_type,         18, FN_HALT},
    {"vsm_height",       VDI_vsm_height,       19, FN_HALT},
    {"vsm_color",        VDI_vsm_color,        20, FN_HALT},
    {"vst_font",         VDI_vst_font,         21, FN_HALT},
    {"vst_color",        VDI_vst_color,        22, FN_HALT},
    {"vsf_interior",     VDI_vsf_interior,     23, FN_HALT},
    {"vsf_style",        VDI_vsf_style,        24, FN_HALT},
    {"vsf_color",        VDI_vsf_color,        25, FN_HALT},
    {"vq_color",         VDI_vq_color,         26, FN_HALT},
    {"vq_cellarray",     VDI_vq_cellarray,     27, FN_HALT},
    {"v_locator",        VDI_v_locator,        28, FN_HALT},
    {"v_valuator",       VDI_v_valuator,       29, FN_HALT},
    {"v_choice",         VDI_v_choice,         30, FN_HALT},
    {"v_string",         VDI_v_string,         31, FN_HALT},
    {"vswr_mode",        VDI_vswr_mode,        32, FN_HALT},
    {"vsin_mode",        VDI_vsin_mode,        33, FN_HALT},
    {"vql_attributes",   VDI_vql_attributes,   35, FN_HALT},
    {"vqm_attributes",   VDI_vqm_attributes,   36, FN_HALT},
    {"vqf_attributes",   VDI_vqf_attributes,   37, FN_HALT},
    {"vqt_attributes",   VDI_vqt_attributes,   38, FN_HALT},
    {"vst_alignment",    VDI_vst_alignment,    39, FN_HALT},

    {"v_opnvwk",         VDI_v_opnvwk,        100, FN_HALT},
    {"v_clsvwk",         VDI_v_clsvwk,        101, FN_HALT},
    {"vq_extnd",         VDI_vq_extnd,        102, FN_HALT},
    {"v_contourfill",    VDI_v_contourfill,   103, FN_HALT},
    {"vsf_perimeter",    VDI_vsf_perimeter,   104, FN_HALT},
    {"v_get_pixel",      VDI_v_get_pixel,     105, FN_HALT},
    {"vst_effects",      VDI_vst_effects,     106, FN_HALT},
    {"vst_point",        VDI_vst_point,       107, FN_HALT},
    {"vsl_ends",         VDI_vsl_ends,        108, FN_HALT},
    {"vro_cpyfm",        VDI_vro_cpyfm,       109, FN_HALT},
    {"vr_trnfm",         VDI_vr_trnfm,        110, FN_HALT},
    {"vsc_form",         VDI_vsc_form,        111, FN_HALT},
    {"vsf_updat",        VDI_vsf_updat,       112, FN_HALT},
    {"vsl_udsty",        VDI_vsl_udsty,       113, FN_HALT},
    {"vr_recfl",         VDI_vr_recfl,        114, FN_HALT},
    {"vqin_mode",        VDI_vqin_mode,       115, FN_HALT},
    {"vqt_extent",       VDI_vqt_extent,      116, FN_HALT},
    {"vqt_width",        VDI_vqt_width,       117, FN_HALT},
    {"vex_timv",         VDI_vex_timv,        118, FN_HALT},
    {"vst_load_fonts",   VDI_vst_load_fonts,  119, FN_HALT},
    {"vst_unload_fonts", VDI_vst_unload_fonts,120, FN_HALT},
    {"vrt_cpyfm",        VDI_vrt_cpyfm,       121, FN_HALT},
    {"v_show_c",         VDI_v_show_c,        122, FN_HALT},
    {"v_hide_c",         VDI_v_hide_c,        123, FN_HALT},
    {"vq_mouse",         VDI_vq_mouse,        124, FN_HALT},
    {"vex_butv",         VDI_vex_butv,        125, FN_HALT},
    {"vex_motv",         VDI_vex_motv,        126, FN_HALT},
    {"vex_curv",         VDI_vex_curv,        127, FN_HALT},
    {"vq_key_s",         VDI_vq_key_s,        128, FN_HALT},
    {"vs_clip",          VDI_vs_clip,         129, FN_HALT},
    {"vqt_name",         VDI_vqt_name,        130, FN_HALT},
    {"vqt_fontinfo",     VDI_vqt_fontinfo,    131, FN_HALT},

    {"vqt_fontheader",   VDI_vqt_fontheader,  232, FN_HALT},
    {"vqt_trackkern",    VDI_vqt_trackkern,   234, FN_HALT},
    {"vqt_pairkern",     VDI_vqt_pairkern,    235, FN_HALT},
    {"vst_charmap",      VDI_vst_charmap,     236, FN_HALT},
    {"vst_kern",         VDI_vst_kern,        237, FN_HALT},
    {"v_getbitmap_info", VDI_v_getbitmap_info,239, FN_HALT},
    {"vqt_f_extent",     VDI_vqt_f_extent,    240, FN_HALT},
    {"v_ftext",          VDI_v_ftext,         241, FN_HALT},
    {"v_ftext_offset",   VDI_v_ftext_offset,  242, FN_HALT},
    {"v_getoutline",     VDI_v_getoutline,    243, FN_HALT},
    {"vst_scratch",      VDI_vst_scratch,     244, FN_HALT},
    {"vst_error",        VDI_vst_error,       245, FN_HALT},
    {"vst_arbpt",        VDI_vst_arbpt,       246, FN_HALT},
    {"vqt_advance",      VDI_vqt_advance,     247, FN_HALT},
    {"vqt_devinfo",      VDI_vqt_devinfo,     248, FN_HALT},
    {"v_savecache",      VDI_v_savecache,     249, FN_HALT},
    {"v_loadcache",      VDI_v_loadcache,     250, FN_HALT},
    {"v_flushcache",     VDI_v_flushcache,    251, FN_HALT},
    {"vst_setsize",      VDI_vst_setsize,     252, FN_HALT},
    {"vst_skew",         VDI_vst_skew,        253, FN_HALT},
    {"vqt_get_table",    VDI_vqt_get_table,   254, FN_HALT},
    {"vqt_cachesize",    VDI_vqt_cachesize,   255, FN_HALT}
};

/*
 * The generalised drawing primitives, which all arrive as opcode 11 and are
 * told apart by control[5]. Naming them makes the diagnostic say v_rbox rather
 * than gdp 8.
 */
static char *gdp_name(int16_t sub)
{
    switch (sub)
    {
        case  1: return "v_bar";
        case  2: return "v_arc";
        case  3: return "v_pieslice";
        case  4: return "v_circle";
        case  5: return "v_ellipse";
        case  6: return "v_ellarc";
        case  7: return "v_ellpie";
        case  8: return "v_rbox";
        case  9: return "v_rfbox";
        case 10: return "v_justified";
        case 13: return "v_bez_on/v_bez_off";
        default: return 0;
    }
}

void vdi_reset()
{
    /* Nothing is held across applications yet. Workstations, their attributes
     * and the surfaces they draw into all arrive with the VDI proper, and this
     * is where they are let go of again. */
}

void vdi_trap()
{
    uint32_t block = m68k_get_reg(0, M68K_REG_D1);
    int i;

    pb.control = m68k_read_memory_32(block +  0);
    pb.intin   = m68k_read_memory_32(block +  4);
    pb.ptsin   = m68k_read_memory_32(block +  8);
    pb.intout  = m68k_read_memory_32(block + 12);
    pb.ptsout  = m68k_read_memory_32(block + 16);

    pb.opcode  = gem_word(pb.control, VDI_CONTROL_WORDS, 0);
    pb.n_ptsin = gem_word(pb.control, VDI_CONTROL_WORDS, 1) * 2;
    pb.n_intin = gem_word(pb.control, VDI_CONTROL_WORDS, 3);
    pb.n_intout = VDI_INTOUT_MAX;
    pb.n_ptsout = VDI_PTSOUT_MAX * 2;

    /* A call that produces nothing has to say so, or the caller reads however
     * many entries the call before it left behind */
    vdi_set_intout_count(0);
    vdi_set_ptsout_count(0);

#ifdef ENABLE_VDI_TRACE
    printf("VDI call %d: handle %d, sub %d, ptsin %d, intin %d\n",
           pb.opcode, vdi_handle(), vdi_subfunction(),
           pb.n_ptsin/2, pb.n_intin);
#endif

    for (i = 0; i < (int)(sizeof(VDI_functions)/sizeof(struct VDI_function)); ++i)
    {
        struct VDI_function *f = &VDI_functions[i];
        uint32_t r;

        if (f->id != pb.opcode)
            continue;

        if (f->fnct)
        {
            r = f->fnct();
        }
        else if (f->kind == FN_STUB)
        {
#ifdef ENABLE_VDI_TRACE
            printf("Stubbed %s (%d)\n", f->name, pb.opcode);
#endif
            return;
        }
        else
        {
            char *sub = (pb.opcode == 11) ? gdp_name(vdi_subfunction()) : 0;

            halt_execution();
            if (sub)
                printf("VDI %s (%d, gdp %d) not implemented\n",
                       sub, pb.opcode, vdi_subfunction());
            else if (pb.opcode == 5 || pb.opcode == 11)
                printf("VDI %s %d (%d) not implemented\n",
                       f->name, vdi_subfunction(), pb.opcode);
            else
                printf("VDI %s (%d) not implemented\n", f->name, pb.opcode);
            return;
        }

#ifdef ENABLE_VDI_TRACE
        printf("Return from %s: %d = 0x%x\n", f->name, r, r);
#else
        (void)r;
#endif

        return;
    }

    halt_execution();
    printf("VDI Unknown function called %d\n", pb.opcode);
}
