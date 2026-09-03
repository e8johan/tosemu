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
#include <string.h>
#include <stdlib.h>

#include "gem_p.h"
#include "vdi_p.h"
#include "tossystem.h"
#include "xbios.h"
#include "emuvdi/emuvdi.h"
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
#define VDI_INTIN_MAX   (1024)
#define VDI_PTSIN_MAX   (1024)
#define VDI_INTOUT_MAX   (512)
#define VDI_PTSOUT_MAX   (256)

int16_t vdi_handle()
{
    return gem_word(pb.control, VDI_CONTROL_WORDS, 6);
}

int16_t vdi_subfunction()
{
    return gem_word(pb.control, VDI_CONTROL_WORDS, 5);
}

/* What the calls are called ***********************************************/

/*
 * Only for saying which call it was. The VDI itself is dispatched by EmuTOS,
 * which knows the numbers; this is so that an application meeting a gap is
 * told that vqt_f_extent is not implemented rather than that function 240 is
 * unknown. http://toshyp.atari.org/en/vdi.html
 */
struct VDI_name {
    char *name;
    uint16_t id;
};

static struct VDI_name VDI_names[] = {
    {"v_opnwk", 1}, {"v_clswk", 2}, {"v_clrwk", 3}, {"v_updwk", 4},
    {"escape", 5}, {"v_pline", 6}, {"v_pmarker", 7}, {"v_gtext", 8},
    {"v_fillarea", 9}, {"v_cellarray", 10}, {"gdp", 11}, {"vst_height", 12},
    {"vst_rotation", 13}, {"vs_color", 14}, {"vsl_type", 15},
    {"vsl_width", 16}, {"vsl_color", 17}, {"vsm_type", 18},
    {"vsm_height", 19}, {"vsm_color", 20}, {"vst_font", 21},
    {"vst_color", 22}, {"vsf_interior", 23}, {"vsf_style", 24},
    {"vsf_color", 25}, {"vq_color", 26}, {"vq_cellarray", 27},
    {"v_locator", 28}, {"v_valuator", 29}, {"v_choice", 30},
    {"v_string", 31}, {"vswr_mode", 32}, {"vsin_mode", 33},
    {"vql_attributes", 35}, {"vqm_attributes", 36}, {"vqf_attributes", 37},
    {"vqt_attributes", 38}, {"vst_alignment", 39},

    {"v_opnvwk", 100}, {"v_clsvwk", 101}, {"vq_extnd", 102},
    {"v_contourfill", 103}, {"vsf_perimeter", 104}, {"v_get_pixel", 105},
    {"vst_effects", 106}, {"vst_point", 107}, {"vsl_ends", 108},
    {"vro_cpyfm", 109}, {"vr_trnfm", 110}, {"vsc_form", 111},
    {"vsf_updat", 112}, {"vsl_udsty", 113}, {"vr_recfl", 114},
    {"vqin_mode", 115}, {"vqt_extent", 116}, {"vqt_width", 117},
    {"vex_timv", 118}, {"vst_load_fonts", 119}, {"vst_unload_fonts", 120},
    {"vrt_cpyfm", 121}, {"v_show_c", 122}, {"v_hide_c", 123},
    {"vq_mouse", 124}, {"vex_butv", 125}, {"vex_motv", 126},
    {"vex_curv", 127}, {"vq_key_s", 128}, {"vs_clip", 129},
    {"vqt_name", 130}, {"vqt_fontinfo", 131},

    /* The Speedo and FontGDOS additions, which arrive on their own range well
     * above the VDI proper and none of which EmuTOS has */
    {"vqt_fontheader", 232}, {"vqt_trackkern", 234}, {"vqt_pairkern", 235},
    {"vst_charmap", 236}, {"vst_kern", 237}, {"v_getbitmap_info", 239},
    {"vqt_f_extent", 240}, {"v_ftext", 241}, {"v_ftext_offset", 242},
    {"v_getoutline", 243}, {"vst_scratch", 244}, {"vst_error", 245},
    {"vst_arbpt", 246}, {"vqt_advance", 247}, {"vqt_devinfo", 248},
    {"v_savecache", 249}, {"v_loadcache", 250}, {"v_flushcache", 251},
    {"vst_setsize", 252}, {"vst_skew", 253}, {"vqt_get_table", 254},
    {"vqt_cachesize", 255}
};

/*
 * The generalised drawing primitives all arrive as opcode 11 and are told
 * apart by the sub function, so naming them makes the diagnostic say v_rbox
 * rather than gdp 8.
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

static char *vdi_name(int16_t opcode)
{
    int i;

    if (opcode == 11)
    {
        char *sub = gdp_name(vdi_subfunction());

        if (sub)
            return sub;
    }

    for (i = 0; i < (int)(sizeof(VDI_names)/sizeof(struct VDI_name)); ++i)
        if (VDI_names[i].id == opcode)
            return VDI_names[i].name;

    return "an unknown function";
}

/* Bitmaps ******************************************************************/

/*
 * The raster operations are handed the address of a form definition block, an
 * MFDB, in two words of the control array, and the VDI reads it as a pointer.
 * An application's is in the emulated machine and describes a bitmap there, so
 * both have to be brought across.
 *
 * This is where the choice to keep surfaces in host byte order is paid for.
 * An application's bitmap is 68000 memory, so its words are the other way
 * round, and every one of them has to be turned over on the way in and on the
 * way back. Reading through the emulator's own accessors is what does it:
 * they already answer in host order, so a word at a time is a copy and a swap
 * at once.
 *
 * An address of zero is not a bitmap but the screen, which is how an
 * application says "where I can see it". So is the address Physbase and
 * Logbase hand out, because on the machine those were the same memory: an
 * application that asks the XBIOS where the screen is and puts the answer in
 * an MFDB has named the screen just as surely as one that wrote a zero, and
 * plenty do - it is how a program that draws on the screen without a window
 * gets at it. Here the two are not the same memory at all, the screen being a
 * surface of the host's that has no address in the machine, so the equivalence
 * has to be made rather than inherited. Without it the raster operations copy
 * to and from the buffer the XBIOS handed out, which nothing ever shows, and
 * the drawing goes quietly nowhere.
 */
#define MFDB_ADDR     (0)
#define MFDB_W        (4)
#define MFDB_H        (6)
#define MFDB_WDWIDTH  (8)
#define MFDB_STAND   (10)
#define MFDB_NPLANES (12)

struct bitmap {
    uint32_t data;      /* Where the bitmap is in the machine, 0 for screen */
    int words;          /* How much of it there is */
    uint16_t *copy;     /* Ours, or null when it is the screen */
};

static struct bitmap bitmaps[2];

/* Reads the MFDB the control array names and brings its bitmap across */
static int bitmap_in(int16_t *control, int index, int slot)
{
    struct bitmap *b = &bitmaps[slot];
    void *host = emuvdi_mfdb(slot);
    uint32_t mfdb = (uint32_t)((uint16_t)control[index] << 16)
                  | (uint16_t)control[index + 1];
    int16_t w, h, wdwidth, stand, planes;
    int i;

    b->data = 0;
    b->words = 0;
    b->copy = 0;

    if (mfdb == 0)
        return 1;   /* The call names no bitmap at all */

    b->data  = m68k_read_memory_32(mfdb + MFDB_ADDR);
    w        = (int16_t)m68k_read_memory_16(mfdb + MFDB_W);
    h        = (int16_t)m68k_read_memory_16(mfdb + MFDB_H);
    wdwidth  = (int16_t)m68k_read_memory_16(mfdb + MFDB_WDWIDTH);
    stand    = (int16_t)m68k_read_memory_16(mfdb + MFDB_STAND);
    planes   = (int16_t)m68k_read_memory_16(mfdb + MFDB_NPLANES);

    if (b->data == 0 || xbios_screen_named(b->data))
    {
        /* The screen. The VDI knows where that is, and is told so the way
         * EmuTOS expects to hear it, which is with no address at all. */
        b->data = 0;
        emuvdi_mfdb_set(host, 0, w, h, wdwidth, stand, planes);
        emuvdi_control_set_pointer(control, index, host);
        return 1;
    }

    if (w <= 0 || h <= 0 || wdwidth <= 0 || planes <= 0)
    {
        halt_execution();
        printf("VDI: a bitmap of %dx%d in %d planes is not one\n", w, h, planes);
        return 0;
    }

    b->words = h * wdwidth * planes;

    b->copy = calloc((size_t)b->words, sizeof *b->copy);
    if (!b->copy)
    {
        halt_execution();
        printf("VDI: no room to copy a %dx%d bitmap across\n", w, h);
        return 0;
    }

    for (i = 0; i < b->words; i++)
        b->copy[i] = (uint16_t)m68k_read_memory_16(b->data + 2*i);

    emuvdi_mfdb_set(host, b->copy, w, h, wdwidth, stand, planes);
    emuvdi_control_set_pointer(control, index, host);

    return 1;
}

/* Puts a bitmap the call drew into back where the application keeps it */
static void bitmap_out(int slot)
{
    struct bitmap *b = &bitmaps[slot];
    int i;

    if (!b->copy)
        return;

    for (i = 0; i < b->words; i++)
        m68k_write_memory_16(b->data + 2*i, b->copy[i]);
}

static void bitmap_done(int slot)
{
    free(bitmaps[slot].copy);
    bitmaps[slot].copy = 0;
}

/*
 * The calls that name bitmaps, and where. vro_cpyfm and vrt_cpyfm copy from
 * the first to the second; vr_trnfm turns one between the layout a resource
 * file uses and the one the screen does, in place.
 */
static int names_bitmaps(int16_t opcode)
{
    return opcode == 109 || opcode == 121 || opcode == 110;
}

/* Handing a call to the VDI ***********************************************/

/*
 * The arrays the VDI is handed, in host memory.
 *
 * A VDI call names its arrays by address in the emulated memory, and the VDI
 * reads and writes them a word at a time through pointers of its own. Copying
 * them across rather than pointing at emulated memory is what keeps the VDI
 * from having to know that the memory it is looking at belongs to a 68000.
 *
 * The sizes are the ceilings NVDI 4 documents. A call asking for more than
 * fits is refused rather than allowed to write past the end.
 */
static int16_t h_control[VDI_CONTROL_WORDS];
static int16_t h_intin[VDI_INTIN_MAX];
static int16_t h_ptsin[VDI_PTSIN_MAX];
static int16_t h_intout[VDI_INTOUT_MAX];
static int16_t h_ptsout[VDI_PTSOUT_MAX];

void vdi_reset()
{
    /* The screen belongs to GEM rather than to either half of it, and
     * gem_reset lets it go */
}

void vdi_trap()
{
    uint32_t block = m68k_get_reg(0, M68K_REG_D1);
    int i, n_ptsin, n_intin, n_ptsout, n_intout;

    pb.control = m68k_read_memory_32(block +  0);
    pb.intin   = m68k_read_memory_32(block +  4);
    pb.ptsin   = m68k_read_memory_32(block +  8);
    pb.intout  = m68k_read_memory_32(block + 12);
    pb.ptsout  = m68k_read_memory_32(block + 16);

    pb.opcode  = gem_word(pb.control, VDI_CONTROL_WORDS, 0);
    n_ptsin = gem_word(pb.control, VDI_CONTROL_WORDS, 1) * 2;
    n_intin = gem_word(pb.control, VDI_CONTROL_WORDS, 3);

    pb.n_ptsin = n_ptsin;
    pb.n_intin = n_intin;
    pb.n_intout = VDI_INTOUT_MAX;
    pb.n_ptsout = VDI_PTSOUT_MAX;

    FUNC_TRACE_ARGS {
        printf("VDI call %d: handle %d, sub %d, ptsin %d, intin %d\n",
               pb.opcode, vdi_handle(), vdi_subfunction(), n_ptsin/2, n_intin);
    }

    if (!emuvdi_implements(pb.opcode))
    {
        halt_execution();
        printf("VDI %s (%d) not implemented\n", vdi_name(pb.opcode), pb.opcode);
        return;
    }

    if (n_ptsin < 0 || n_ptsin > VDI_PTSIN_MAX ||
        n_intin < 0 || n_intin > VDI_INTIN_MAX)
    {
        halt_execution();
        printf("VDI %s (%d) asked to read %d points and %d values, which is "
               "more than any VDI call takes\n",
               vdi_name(pb.opcode), pb.opcode, n_ptsin/2, n_intin);
        return;
    }

    if (!gem_start())
        return;

    /* In */
    for (i = 0; i < VDI_CONTROL_WORDS; i++)
        h_control[i] = gem_word(pb.control, VDI_CONTROL_WORDS, i);
    for (i = 0; i < n_intin; i++)
        h_intin[i] = gem_word(pb.intin, n_intin, i);
    for (i = 0; i < n_ptsin; i++)
        h_ptsin[i] = gem_word(pb.ptsin, n_ptsin, i);

    memset(h_intout, 0, sizeof h_intout);
    memset(h_ptsout, 0, sizeof h_ptsout);

    if (names_bitmaps(pb.opcode))
    {
        if (!bitmap_in(h_control, 7, 0) || !bitmap_in(h_control, 9, 1))
        {
            bitmap_done(0);
            bitmap_done(1);
            return;
        }
    }

    emuvdi_call(h_control, h_intin, h_ptsin, h_intout, h_ptsout);

    if (names_bitmaps(pb.opcode))
    {
        /* Only the destination was drawn into. vr_trnfm may have been given
         * the same bitmap twice, and writing the source back would undo it. */
        bitmap_out(1);
        bitmap_done(0);
        bitmap_done(1);
    }

    /* Out. The VDI says how much it answered with in the control array, and
     * the caller reads that to know how much of the arrays to look at. */
    n_ptsout = h_control[2] * 2;
    n_intout = h_control[4];

    if (n_ptsout > VDI_PTSOUT_MAX)
        n_ptsout = VDI_PTSOUT_MAX;
    if (n_intout > VDI_INTOUT_MAX)
        n_intout = VDI_INTOUT_MAX;

    for (i = 0; i < n_intout; i++)
        gem_set_word(pb.intout, n_intout, i, h_intout[i]);
    for (i = 0; i < n_ptsout; i++)
        gem_set_word(pb.ptsout, n_ptsout, i, h_ptsout[i]);

    gem_set_word(pb.control, VDI_CONTROL_WORDS, 2, h_control[2]);
    gem_set_word(pb.control, VDI_CONTROL_WORDS, 4, h_control[4]);

    /* Opening a workstation answers with its handle here rather than in
     * intout, which is why every binding reads it back out of the control
     * array afterwards */
    gem_set_word(pb.control, VDI_CONTROL_WORDS, 6, h_control[6]);

    FUNC_TRACE_ARGS {
        printf("Return from %s: %d values, %d points\n",
               vdi_name(pb.opcode), n_intout, n_ptsout/2);
    }
}
