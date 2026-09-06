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
 * The SpeedoGDOS calls, and the ordinary text calls while an outline face is
 * the one selected.
 *
 * These arrive on a range of their own, from 232 up, well above the VDI
 * proper. EmuTOS has none of them - its jump tables stop at 134 - and there is
 * nowhere to put them in 3rdparty, so the dispatcher is here and tosemu's
 * emuvdi_call gives it first refusal.
 *
 * The state a workstation keeps for these is here too, in a table of its own
 * keyed by handle, because Vwk is EmuTOS's structure and cannot be given
 * fields. What it holds is what SpeedoGDOS held: which face, at what size, and
 * where to put an error.
 *
 * The second half of the file is the awkward part and is not avoidable. An
 * application that has chosen an outline face with vst_font goes on calling
 * v_gtext and vqt_extent, which are ordinary VDI calls that EmuTOS implements
 * against a Fonthead - and an outline face has no Fonthead, there being no
 * raster to point one at. So those calls are answered here whenever the
 * selected face is an outline, and fall through to EmuTOS the rest of the
 * time, which is nearly always.
 *
 * Drawing goes through vdi_vrt_cpyfm rather than onto the surface directly.
 * That is what gives clipping, the writing mode and the text colour without
 * any of them being worked out twice: the string is rendered into a monochrome
 * bitmap and the VDI's own transparent blit puts it on the screen. The five
 * parameter block globals belong to the caller at that moment, so they are
 * saved and put back around it - anything else loses the arguments of the call
 * being served.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "string.h"
#include "gsxdefs.h"
#include "xbiosbind.h"

#include "../fontface.h"

#include "gdos.h"
#include "emuvdi.h"

/* EmuTOS's, for putting a bitmap on the screen with the workstation's own
 * clipping and colours - vdi_raster.c, which tosemu carries a copy of */
void vdi_vrt_cpyfm(Vwk *vwk);

/* The opcodes, named rather than numbered where they are used */
#define VQT_EXTENT_OP       (116)
#define VQT_WIDTH_OP        (117)
#define VQT_NAME_OP         (130)
#define VQT_FONTINFO_OP     (131)
#define V_GTEXT_OP            (8)
#define VST_HEIGHT_OP        (12)
#define VST_FONT_OP          (21)
#define VST_POINT_OP        (107)

#define VQT_FONTHEADER_OP   (232)
#define VQT_TRACKKERN_OP    (234)
#define VQT_PAIRKERN_OP     (235)
#define VST_CHARMAP_OP      (236)
#define VST_KERN_OP         (237)
#define V_GETBITMAP_INFO_OP (239)
#define VQT_F_EXTENT_OP     (240)
#define V_FTEXT_OP          (241)
#define V_FTEXT_OFFSET_OP   (242)
#define V_GETOUTLINE_OP     (243)
#define VST_SCRATCH_OP      (244)
#define VST_ERROR_OP        (245)
#define VST_ARBPT_OP        (246)
#define VQT_ADVANCE_OP      (247)
#define VQT_DEVINFO_OP      (248)
#define V_SAVECACHE_OP      (249)
#define V_LOADCACHE_OP      (250)
#define V_FLUSHCACHE_OP     (251)
#define VST_SETSIZE_OP      (252)
#define VST_SKEW_OP         (253)
#define VQT_GET_TABLE_OP    (254)
#define VQT_CACHESIZE_OP    (255)

/* What SpeedoGDOS put in the word vst_error names */
#define FSM_NO_ERROR            (0)
#define FSM_UNSUPPORTED        (-1)

/*
 * One of these to a workstation. NUM_VDI_HANDLES is what EmuTOS allows, and
 * handles are small numbers from one, so an array indexed by handle is the
 * whole of the bookkeeping.
 */
struct fsm {
    WORD loaded;        /* whether vst_load_fonts has been called here */
    WORD id;            /* the outline face selected, 0 for none */
    LONG size64;        /* the size, in 64ths of a point */
    WORD error;         /* what vst_error would report */
    WORD charmap;       /* which character set the strings are in */
    WORD skew;
    WORD kerning;
};

static struct fsm state[NUM_VDI_HANDLES + 2];

static struct fsm *fsm_of(WORD handle)
{
    if (handle < 0 || handle >= (WORD)(sizeof state / sizeof state[0]))
        return 0;

    return &state[handle];
}

/*
 * The size an application last asked for, defaulted to something that can be
 * drawn. A workstation that selects a face without setting a size has no size,
 * and ten point is what the smallest .FNT beside it would have been.
 */
#define FSM_DEFAULT_SIZE64 (10 * 64)

static LONG size_of(const struct fsm *f)
{
    return f->size64 > 0 ? f->size64 : FSM_DEFAULT_SIZE64;
}

/* An Atari string arrives one character to a word, which is how every VDI text
 * call takes one. This is the whole of the conversion. */
static int gather(const WORD *intin, int count, char *into, int room)
{
    int i;

    if (count > room - 1)
        count = room - 1;

    for (i = 0; i < count; i++)
        into[i] = (char)(intin[i] & 0xff);

    into[count] = 0;

    return count;
}

/* Long enough for any line an application draws in one call, and bounded so
 * that a count out of a parameter block cannot ask for the stack */
#define FSM_MAX_TEXT (512)


/* The size the ordinary calls speak in ****************************************/

/*
 * vst_height asks in pixels rather than in points, so it has to be turned into
 * one. A point is a seventy second of an inch and the screen says how many
 * dots that is, which is the same arithmetic the other way round from the one
 * fontface_resolution was told.
 */
static int screen_ydpi(void);

static LONG size_from_height(WORD height)
{
    int dpi = screen_ydpi();

    if (dpi <= 0)
        dpi = 72;

    return ((LONG)height * 72 * 64) / dpi;
}


/* Where the screen is, in dots to the inch ***********************************/

/*
 * An ST high screen is 640 by 400 in the space a monitor showed at about
 * seventy two dots to the inch each way, which is why a ten point .FNT for it
 * is thirteen scan lines tall. The medium screen is the same width and half
 * the height, so a character has to be half as tall in pixels to be the same
 * height on the glass - which is exactly what Atari shipped the second set of
 * font files for. Here it is arithmetic instead.
 */
#define FSM_BASE_DPI (72)

static int screen_ydpi(void)
{
    /* Two hundred lines where a high resolution screen has four hundred, in
     * the same physical space */
    return (V_REZ_VT < 300) ? FSM_BASE_DPI / 2 : FSM_BASE_DPI;
}

static int screen_xdpi(void)
{
    /* And three hundred and twenty columns where it has six hundred and forty */
    return (V_REZ_HZ < 400) ? FSM_BASE_DPI / 2 : FSM_BASE_DPI;
}

void gdos_fsm_init(void)
{
    if (fontface_init() > 0)
        fontface_resolution(screen_xdpi(), screen_ydpi());
}

int gdos_fsm_faces(void)
{
    return fontface_count();
}


/* Drawing *******************************************************************/

/*
 * A rendered string, put on the screen through the VDI's own transparent blit.
 *
 * Everything that makes drawing look right belongs to the workstation rather
 * than to the string - the clipping rectangle, the writing mode, the colour -
 * and vdi_vrt_cpyfm reads all three out of the Vwk. So the string goes into a
 * monochrome bitmap and that call puts it down, which is both less code and
 * less to get wrong than reaching for the surface.
 *
 * The five parameter block pointers are the caller's while this runs, so they
 * are saved and put back. Forgetting that loses the arguments of the very call
 * being served, which reads as the string being drawn from nowhere.
 */
/* A pen, bounded so that it is one of the ones the maps have room for. The
 * size of those tables is private to vdi_col.c; how many colours the screen
 * has is not, and is never larger. */
static WORD pen_index(WORD pen)
{
    if (pen < 0 || pen >= (WORD)numcolors)
        return 1;

    return pen;
}

static void blit(Vwk *vwk, const struct fontface_bitmap *bitmap,
                 WORD x, WORD y)
{
    WORD *save_contrl = CONTRL, *save_intin = INTIN, *save_ptsin = PTSIN;
    WORD *save_intout = INTOUT, *save_ptsout = PTSOUT;
    WORD contrl[16];
    WORD intin[8], ptsin[8], intout[8], ptsout[8];

    /*
     * Static rather than on the stack, and that is not a style choice. A
     * bitmap's address travels to the raster code in two words of the control
     * array, so it has to fit in thirty two bits - which is what -no-pie is
     * for and why everything else GEM keeps an address of comes from
     * host_vdi_alloc. The stack is nowhere near that low: a local here comes
     * back as an address with its top half cut off, and the first thing the
     * blit does is read a width out of it.
     *
     * One of each is enough because only one string is drawn at a time.
     */
    static FDB source, destination;

    memset(contrl, 0, sizeof contrl);
    memset(intin, 0, sizeof intin);
    memset(ptsin, 0, sizeof ptsin);

    source.fd_addr = bitmap->bits;
    source.fd_w = (WORD)(bitmap->words * 16);
    source.fd_h = (WORD)bitmap->height;
    source.fd_wdwidth = (WORD)bitmap->words;
    source.fd_stand = 0;
    source.fd_nplanes = 1;
    source.fd_r1 = source.fd_r2 = source.fd_r3 = 0;

    /* A null address is the screen, which is the convention the whole of the
     * raster half of the VDI uses */
    destination.fd_addr = 0;
    destination.fd_w = V_REZ_HZ;
    destination.fd_h = V_REZ_VT;
    destination.fd_wdwidth = V_REZ_HZ / 16;
    destination.fd_stand = 0;
    destination.fd_nplanes = v_planes;
    destination.fd_r1 = destination.fd_r2 = destination.fd_r3 = 0;

    contrl[0] = 121;                    /* vrt_cpyfm */
    contrl[1] = 4;                      /* four points in ptsin */
    contrl[3] = 3;                      /* three values in intin */
    contrl[6] = vwk->handle;

    *(uint32_t *)&contrl[7] = (uint32_t)(uintptr_t)&source;
    *(uint32_t *)&contrl[9] = (uint32_t)(uintptr_t)&destination;

    /*
     * The workstation's own writing mode, which is kept one lower in the Vwk
     * than the number this call takes.
     *
     * The colours are the awkward part. A Vwk keeps text_color as a pen - the
     * hardware register the drawing writes - because that is what everything
     * that puts pixels down wants. This call does not: it takes a VDI colour
     * index in intin[1] and maps it to a pen itself, so handing it the pen
     * maps it a second time and the text comes out in whatever colour that
     * lands on. On a sixteen colour screen black went in and pink came out.
     * REV_MAP_COL is the way back to the index, and is there for this.
     */
    intin[0] = vwk->wrt_mode + 1;
    intin[1] = REV_MAP_COL[pen_index(vwk->text_color)];
    intin[2] = 0;

    /*
     * Cut to the screen, both rectangles together.
     *
     * The raster call clips to the workstation's rectangle only when the
     * workstation has one - vs_clip is what turns that on, and most drawing is
     * done without it - so a string that reaches past an edge is otherwise
     * copied past the edge of the surface. It reaches past the left edge more
     * often than one would think: the bitmap starts a character's width before
     * where the string does, because a letter may lean left of its own origin,
     * so a string drawn at x of ten begins at a negative x.
     *
     * Whatever comes off one rectangle comes off the other in the same place,
     * which is what keeps the picture where the application put it rather than
     * sliding it along.
     */
    {
        WORD sx1 = 0, sy1 = 0;
        WORD sx2 = (WORD)(bitmap->width - 1), sy2 = (WORD)(bitmap->height - 1);
        WORD dx1 = x, dy1 = y;
        WORD dx2 = (WORD)(x + bitmap->width - 1);
        WORD dy2 = (WORD)(y + bitmap->height - 1);

        if (dx1 < 0)
        {
            sx1 = (WORD)(sx1 - dx1);
            dx1 = 0;
        }

        if (dy1 < 0)
        {
            sy1 = (WORD)(sy1 - dy1);
            dy1 = 0;
        }

        if (dx2 > V_REZ_HZ - 1)
        {
            sx2 = (WORD)(sx2 - (dx2 - (V_REZ_HZ - 1)));
            dx2 = (WORD)(V_REZ_HZ - 1);
        }

        if (dy2 > V_REZ_VT - 1)
        {
            sy2 = (WORD)(sy2 - (dy2 - (V_REZ_VT - 1)));
            dy2 = (WORD)(V_REZ_VT - 1);
        }

        /* Nothing of it is on the screen at all */
        if (dx1 > dx2 || dy1 > dy2)
        {
            CONTRL = save_contrl;
            INTIN = save_intin;
            PTSIN = save_ptsin;
            INTOUT = save_intout;
            PTSOUT = save_ptsout;
            return;
        }

        ptsin[0] = sx1;
        ptsin[1] = sy1;
        ptsin[2] = sx2;
        ptsin[3] = sy2;

        ptsin[4] = dx1;
        ptsin[5] = dy1;
        ptsin[6] = dx2;
        ptsin[7] = dy2;
    }

    CONTRL = contrl;
    INTIN = intin;
    PTSIN = ptsin;
    INTOUT = intout;
    PTSOUT = ptsout;

    vdi_vrt_cpyfm(vwk);

    CONTRL = save_contrl;
    INTIN = save_intin;
    PTSIN = save_ptsin;
    INTOUT = save_intout;
    PTSOUT = save_ptsout;
}

/*
 * Where a string goes, given where the application said to put it.
 *
 * The alignment the workstation carries says which part of the string that
 * point names - the left end or the middle or the right, the baseline or the
 * top or the bottom - and it is the same set of rules the VDI applies to the
 * bitmap fonts. What is different is only where the numbers come from.
 */
static void aligned(Vwk *vwk, const struct fontface_bitmap *bitmap,
                    int width, const struct fontface_metrics *m,
                    WORD *x, WORD *y)
{
    switch (vwk->h_align)
    {
        case 1: *x -= (WORD)(width / 2); break;     /* centred */
        case 2: *x -= (WORD)width; break;           /* right */
        default: break;                             /* left */
    }

    switch (vwk->v_align)
    {
        case 0: *y -= (WORD)m->top; break;          /* the baseline */
        case 1: *y -= (WORD)(m->top + m->descent); break;   /* half line */
        case 2: *y -= (WORD)(m->top + m->descent); break;   /* the bottom */
        case 3: *y -= (WORD)(m->top - m->half); break;      /* half of a lower case */
        case 4: *y -= (WORD)0; break;                       /* the ascent line */
        default: break;                                     /* the top */
    }

    *x -= (WORD)bitmap->origin_x;
    *y = (WORD)(*y + m->top - bitmap->origin_y);
}

static void draw_string(Vwk *vwk, struct fsm *f, WORD x, WORD y,
                        const char *text, int length)
{
    struct fontface_bitmap bitmap;
    struct fontface_metrics m;
    int width = 0;

    if (!fontface_metrics(f->id, (int)size_of(f), &m))
        return;

    if (!fontface_extent(f->id, (int)size_of(f), text, length, &width, 0))
        return;

    if (!fontface_render(f->id, (int)size_of(f), text, length, &bitmap))
        return;

    aligned(vwk, &bitmap, width, &m, &x, &y);
    blit(vwk, &bitmap, x, y);

    fontface_render_free(&bitmap);
}


/* The extent answers *********************************************************/

/*
 * The four corners of the box a string occupies, going round it, which is the
 * shape both vqt_extent and vqt_f_extent answer in. A rotated string would
 * turn the box with it; nothing here rotates one, outline text being drawn
 * upright until something asks otherwise.
 */
static void extent_into(WORD *ptsout, int width, int height)
{
    ptsout[0] = 0;          ptsout[1] = 0;
    ptsout[2] = (WORD)width; ptsout[3] = 0;
    ptsout[4] = (WORD)width; ptsout[5] = (WORD)height;
    ptsout[6] = 0;          ptsout[7] = (WORD)height;
}


/* The calls above 230 ********************************************************/

static void answer(WORD *control, WORD points, WORD values)
{
    control[2] = points;
    control[4] = values;
}

/*
 * Serves one of the SpeedoGDOS calls. Answers 0 for an opcode outside the
 * range, which is how tosemu knows to hand it to EmuTOS instead.
 */
int gdos_fsm_call(WORD *control, WORD *intin, WORD *ptsin,
                  WORD *intout, WORD *ptsout)
{
    WORD opcode = control[0];
    Vwk *vwk;
    struct fsm *f;
    char text[FSM_MAX_TEXT];
    int length, width = 0, height = 0;

    if (opcode < VQT_FONTHEADER_OP || opcode > VQT_CACHESIZE_OP)
        return 0;

    vwk = get_vwk_by_handle(control[6]);
    f = fsm_of(control[6]);

    if (!vwk || !f)
    {
        answer(control, 0, 0);
        return 1;
    }

    answer(control, 0, 0);
    f->error = FSM_NO_ERROR;

    switch (opcode)
    {
        case VST_ARBPT_OP:
            /* A size in whole points, which is what an application asks for
             * when it has a menu of sizes rather than a slider */
            f->size64 = (LONG)intin[0] * 64;
            intout[0] = intin[0];
            goto report_size;

        case VST_SETSIZE_OP:
            /* And the same in 64ths, for a size that is not a whole number of
             * points - which is the whole reason an outline font is here */
            f->size64 = intin[0];
            intout[0] = (WORD)(f->size64 / 64);
            goto report_size;

report_size:
            {
                struct fontface_metrics m;

                if (f->id && fontface_metrics(f->id, (int)size_of(f), &m))
                {
                    ptsout[0] = (WORD)m.max_width;
                    ptsout[1] = (WORD)m.top;
                    ptsout[2] = (WORD)m.max_width;
                    ptsout[3] = (WORD)(m.top + m.descent + 1);
                }

                answer(control, 2, 1);
            }
            return 1;

        case VQT_F_EXTENT_OP:
            length = gather(intin, control[3], text, sizeof text);

            if (f->id)
                fontface_extent(f->id, (int)size_of(f), text, length,
                                &width, &height);

            extent_into(ptsout, width, height);
            answer(control, 4, 0);
            return 1;

        case V_FTEXT_OP:
        case V_FTEXT_OFFSET_OP:
            /*
             * The offset form gives a position for every character, which is
             * how a program that has justified a line itself draws it. Nothing
             * here reads them yet, so it draws the string as one run - which
             * is the same picture for text nobody has moved.
             */
            length = gather(intin, control[3], text, sizeof text);

            if (f->id)
                draw_string(vwk, f, ptsin[0], ptsin[1], text, length);

            return 1;

        case VQT_ADVANCE_OP:
            if (f->id)
                width = fontface_advance(f->id, (int)size_of(f), intin[0]);

            ptsout[0] = (WORD)width;
            ptsout[1] = 0;
            answer(control, 1, 0);
            return 1;

        case VST_ERROR_OP:
            /*
             * Where to put the code when something goes wrong. The address is
             * one in the emulated machine and nothing here can write to it, so
             * what is kept is the mode: an application that asked to be told
             * in a variable is told nothing, and one that asks vst_error for
             * the code gets it.
             */
            intout[0] = f->error;
            answer(control, 0, 1);
            return 1;

        case VST_CHARMAP_OP:
            f->charmap = intin[0];
            intout[0] = f->charmap;
            answer(control, 0, 1);
            return 1;

        case VST_SKEW_OP:
            f->skew = intin[0];
            intout[0] = f->skew;
            answer(control, 0, 1);
            return 1;

        case VST_KERN_OP:
            /*
             * Kerning, which is answered by saying there is none. The pairs a
             * face carries are not read, so a program told there were some
             * would ask for pairs and be given zeroes - saying so up front is
             * the honest half of the same answer.
             */
            f->kerning = 0;
            intout[0] = 0;
            intout[1] = 0;
            answer(control, 0, 2);
            return 1;

        case VQT_TRACKKERN_OP:
        case VQT_PAIRKERN_OP:
            ptsout[0] = 0;
            ptsout[1] = 0;
            answer(control, 1, 0);
            return 1;

        case VST_SCRATCH_OP:
            /* Which buffers to keep, on a machine where the memory a font is
             * built in is the host's and is not scarce */
            intout[0] = intin[0];
            answer(control, 0, 1);
            return 1;

        case V_SAVECACHE_OP:
        case V_LOADCACHE_OP:
        case V_FLUSHCACHE_OP:
            /* There is a cache and it is FreeType's, so there is nothing to
             * write out, read in or throw away on an application's say-so */
            intout[0] = 0;
            answer(control, 0, 1);
            return 1;

        case VQT_CACHESIZE_OP:
            intout[0] = 0;
            ptsout[0] = 0;
            ptsout[1] = 0;
            answer(control, 1, 1);
            return 1;

        case VQT_DEVINFO_OP:
            /*
             * Whether a device is there. There is one output device and it is
             * the screen, so the screen's own number is the only one that gets
             * a yes - which is the same number the ASSIGN.SYS section was
             * chosen by.
             */
            intout[0] = (intin[0] == (WORD)(Getrez() + 2)) ? 1 : 0;
            intout[1] = 0;
            answer(control, 0, 2);
            return 1;

        case VQT_GET_TABLE_OP:
            /*
             * A pointer to the character mapping table, which would have to be
             * an address in the emulated machine and is an address here. There
             * is nothing to hand over that the application could read.
             */
            f->error = FSM_UNSUPPORTED;
            answer(control, 0, 0);
            return 1;

        case VQT_FONTHEADER_OP:
        case V_GETBITMAP_INFO_OP:
        case V_GETOUTLINE_OP:
            /*
             * The three that hand something back through a buffer the
             * application supplied. Its address arrives in two words of the
             * control array, and reading it back is the same eight byte
             * problem that keeps vst_load_fonts out of EmuTOS's hands - except
             * that here the buffer is in the machine's memory as well, so
             * filling it in means crossing the seam in the other direction.
             *
             * They are refused rather than half done. An application that
             * checks vst_error is told; one that does not gets an untouched
             * buffer, which is what it would have got from a driver that did
             * not implement them either.
             */
            f->error = FSM_UNSUPPORTED;
            answer(control, 0, 0);
            return 1;

        default:
            f->error = FSM_UNSUPPORTED;
            answer(control, 0, 0);
            return 1;
    }
}


/* The ordinary text calls, while an outline face is the one selected *********/

/*
 * Which face an element of the list is.
 *
 * The list is the bitmap faces first and the outline ones after them, so where
 * the join is depends on how many bitmap faces this workstation has - which
 * changes when vst_load_fonts is called. An application enumerates once, after
 * loading, and sees one list.
 */
static int outline_element(const Vwk *vwk, WORD element)
{
    return element - vwk->num_fonts - 1;
}

int gdos_fsm_text_call(WORD *control, WORD *intin, WORD *ptsin,
                       WORD *intout, WORD *ptsout)
{
    WORD opcode = control[0];
    Vwk *vwk;
    struct fsm *f;
    struct fontface_metrics m;
    char text[FSM_MAX_TEXT];
    int length, index, width = 0, height = 0;

    if (fontface_count() == 0)
        return 0;

    vwk = get_vwk_by_handle(control[6]);
    f = fsm_of(control[6]);

    if (!vwk || !f)
        return 0;

    /*
     * vqt_name and vst_font are answered here whatever face is selected,
     * because they are how an outline face is found and chosen in the first
     * place. Everything below them is only for a workstation that has one
     * selected already.
     */
    if (opcode == VQT_NAME_OP)
    {
        index = outline_element(vwk, intin[0]);

        if (!f->loaded || index < 0 || index >= fontface_count())
            return 0;       /* one of EmuTOS's, or past the end of the list */

        {
            const char *name = fontface_name(index);
            int i;

            intout[0] = (WORD)fontface_id(index);

            for (i = 0; i < 32; i++)
                intout[i + 1] = name[i] ? (WORD)name[i] : 0;

            answer(control, 0, 33);
        }

        return 1;
    }

    if (opcode == VST_FONT_OP)
    {
        if (!fontface_known(intin[0]))
        {
            /* A bitmap face, or one that is not there at all. Selecting it
             * puts the outline side away again so that the ordinary calls go
             * back to EmuTOS. */
            f->id = 0;
            return 0;
        }

        f->id = intin[0];
        intout[0] = f->id;
        answer(control, 0, 1);

        return 1;
    }

    if (!f->id)
        return 0;

    switch (opcode)
    {
        case VST_POINT_OP:
            f->size64 = (LONG)intin[0] * 64;
            intout[0] = intin[0];
            break;

        case VST_HEIGHT_OP:
            f->size64 = size_from_height(ptsin[1]);
            break;

        case VQT_EXTENT_OP:
            length = gather(intin, control[3], text, sizeof text);
            fontface_extent(f->id, (int)size_of(f), text, length,
                            &width, &height);
            extent_into(ptsout, width, height);
            answer(control, 4, 0);
            return 1;

        case VQT_WIDTH_OP:
            width = fontface_advance(f->id, (int)size_of(f), intin[0]);
            intout[0] = intin[0];
            ptsout[0] = (WORD)width;
            ptsout[1] = 0;      /* no left offset */
            ptsout[2] = 0;      /* nor right */
            ptsout[3] = 0;
            ptsout[4] = 0;
            ptsout[5] = 0;
            answer(control, 3, 1);
            return 1;

        case V_GTEXT_OP:
            length = gather(intin, control[3], text, sizeof text);
            draw_string(vwk, f, ptsin[0], ptsin[1], text, length);
            answer(control, 0, 0);
            return 1;

        case VQT_FONTINFO_OP:
            if (!fontface_metrics(f->id, (int)size_of(f), &m))
                return 0;

            intout[0] = (WORD)m.first;
            intout[1] = (WORD)m.last;

            ptsout[0] = (WORD)m.max_width;
            ptsout[1] = (WORD)m.descent;
            ptsout[2] = 0;                  /* nothing added for thickening */
            ptsout[3] = (WORD)m.descent;
            ptsout[4] = 0;                  /* nor for skewing, either side */
            ptsout[5] = (WORD)m.half;
            ptsout[6] = 0;
            ptsout[7] = (WORD)m.ascent;
            ptsout[8] = 0;
            ptsout[9] = (WORD)m.top;

            answer(control, 5, 2);
            return 1;

        default:
            return 0;
    }

    /* The two that set a size answer with what the size came to, in the same
     * four numbers vst_point and vst_height always answered with */
    if (fontface_metrics(f->id, (int)size_of(f), &m))
    {
        ptsout[0] = (WORD)m.max_width;
        ptsout[1] = (WORD)m.top;
        ptsout[2] = (WORD)m.max_width;
        ptsout[3] = (WORD)(m.top + m.descent + 1);
    }

    answer(control, 2, opcode == VST_POINT_OP ? 1 : 0);

    return 1;
}

/*
 * The outline faces arriving, and going away again.
 *
 * They are not in any font ring - there is no Fonthead to put in one - so
 * nothing EmuTOS does makes them appear or disappear, and vst_load_fonts is
 * where an application expects both to happen. Until it has been called this
 * workstation has the faces it was opened with and no others, which is the
 * order SpeedoGDOS documented: load the fonts before asking anything about
 * them.
 */
int gdos_fsm_load(WORD handle)
{
    struct fsm *f = fsm_of(handle);

    if (!f || f->loaded)
        return 0;       /* one chance, the same as the fonts from files get */

    f->loaded = 1;

    return fontface_count();
}

void gdos_fsm_unload(WORD handle)
{
    struct fsm *f = fsm_of(handle);

    if (!f)
        return;

    f->loaded = 0;
    f->id = 0;          /* nothing selected that is no longer there */
}
