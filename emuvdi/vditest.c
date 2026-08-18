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

/* Does the EmuTOS VDI still draw, built for the host?
 *
 * Whether it compiles is not the question. Whether the plane addressing and
 * the word order survive the move off a big endian machine is, and that is not
 * something a build answers. So this draws into a surface and prints it, and
 * the expected output beside it is what the answer is checked against.
 *
 * The cases are chosen to fail loudly rather than subtly:
 *
 *   a rectangle in colour 1     the simple case
 *   the same in colour 5        lights planes 0 and 2 and leaves 1 and 3, so
 *                               an addressing mistake is the wrong shade
 *                               rather than nothing at all
 *   a rectangle x 5..60         crosses four word boundaries at an odd offset,
 *                               where the end masks have to be right
 *   a polyline                  the Bresenham code rather than the fill
 *   text                        the system font through v_gtext, and then
 *                               each of the effects, which is the only check
 *                               there is on the normal_blit written here
 *
 * Pixels are read back out of the plane words rather than through the VDI, so
 * that what is printed is evidence about the memory.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "lineavars.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* The surface. Small enough to print, wide enough to cross a word boundary
 * several times, which is where plane addressing goes wrong if it is going to.
 */
#define WIDTH   (64)
#define HEIGHT  (24)
#define PLANES  (4)

#define WORDS_PER_LINE (WIDTH/16*PLANES)

static UWORD surface[HEIGHT * WORDS_PER_LINE];

void host_surface_select(void *base, UWORD width, UWORD height, UWORD planes);

/* The AES's own VDI interface, aes/gemgsxif.c */
void gsx_init(void);
void ob_draw(OBJECT *tree, WORD obj, WORD depth);

/* Reads a pixel back the long way round, from the plane words, so that the
 * printout is evidence about the memory rather than about the drawing code */
static int pixel(int x, int y)
{
    UWORD *word = surface + y*WORDS_PER_LINE + (x/16)*PLANES;
    int mask = 0x8000 >> (x & 15);
    int plane, value = 0;

    for (plane = 0; plane < PLANES; plane++)
        if (word[plane] & mask)
            value |= 1 << plane;

    return value;
}

static void show(const char *what)
{
    static const char shade[] = " .:-=+*#%@$&XW8M";
    int x, y;

    printf("\n%s\n   +", what);
    for (x = 0; x < WIDTH; x++)
        printf("-");
    printf("+\n");

    for (y = 0; y < HEIGHT; y++)
    {
        printf("%2d |", y);
        for (x = 0; x < WIDTH; x++)
            putchar(shade[pixel(x, y) & 15]);
        printf("|\n");
    }

    printf("   +");
    for (x = 0; x < WIDTH; x++)
        printf("-");
    printf("+\n");
}

/*
 * The AES kernel's waiting, which the object library is linked against and
 * this never reaches: nothing here waits for anything. It is here because the
 * linker wants it, and it says so rather than returning something that could
 * be mistaken for an event.
 */
int16_t host_event_wait(int16_t wanted, int32_t timeout, int16_t *message,
                        const int16_t *m1, int16_t m1flags,
                        const int16_t *m2, int16_t m2flags,
                        int16_t *key, int16_t *mx, int16_t *my,
                        int16_t *buttons, int16_t *kstate)
{
    fprintf(stderr, "vditest: something waited for an event, and this test has "
            "no way to deliver one\n");

    return 0;
}

/* The parameter arrays a VDI call arrives in. Drawing text goes through the
 * real entry point rather than through a helper, so it needs them. */
static WORD contrl[16], intin[128], ptsin[16], intout[128], ptsout[16];

void host_font_init(void);

static Vwk vwk;

static void workstation_init(void)
{
    memset(&vwk, 0, sizeof vwk);

    vwk.handle = 1;

    /* The workstation holds the writing mode a step below the number an
     * application passes, so replace mode is 0 here and 1 to a caller */
    vwk.wrt_mode = WM_REPLACE;

    vwk.clip = 1;
    vwk.xmn_clip = 0;
    vwk.ymn_clip = 0;
    vwk.xmx_clip = WIDTH - 1;
    vwk.ymx_clip = HEIGHT - 1;

    vwk.fill_color = 1;
    vwk.fill_style = FIS_SOLID;
    vwk.fill_index = 0;
    vwk.fill_per = 0;
    vwk.multifill = 0;

    vwk.line_color = 1;
    vwk.line_width = 1;
    vwk.line_index = 0;
    vwk.line_beg = SQUARED;
    vwk.line_end = SQUARED;

    vwk.text_color = 1;

    /* Turns fill_style and fill_index into the pattern the fill code reads */
    st_fl_ptr(&vwk);

    CONTRL = contrl;
    INTIN = intin;
    PTSIN = ptsin;
    INTOUT = intout;
    PTSOUT = ptsout;

    /* Gives the workstation the system font and the text defaults */
    text_init2(&vwk);

    /*
     * The line mask is set by the VDI entry point rather than by polyline, so
     * calling the drawing helper directly means setting it here. Our own
     * v_pline handler will have to do the same.
     */
    LN_MASK = LINE_STYLE[vwk.line_index];
}

/* v_gtext as an application makes it: the string in intin, the position in
 * ptsin, and how many characters there are in the control array */
static void gtext(WORD x, WORD y, const char *text)
{
    int i;

    for (i = 0; text[i]; i++)
        intin[i] = (unsigned char)text[i];

    contrl[3] = i;
    ptsin[0] = x;
    ptsin[1] = y;

    vdi_v_gtext(&vwk);
}

int main(void)
{
    Rect rect;
    Point line[5];

    host_surface_select(surface, WIDTH, HEIGHT, PLANES);
    host_font_init();

    workstation_init();

    /* A solid rectangle, in colour 1 */
    memset(surface, 0, sizeof surface);
    rect.x1 = 3;  rect.y1 = 2;
    rect.x2 = 28; rect.y2 = 9;
    draw_rect(&vwk, &rect, 1);
    show("draw_rect, solid, colour 1, x 3..28 y 2..9");

    /* The same rectangle in colour 5, which lights planes 0 and 2, so a plane
     * addressing mistake shows up as the wrong shade rather than not at all */
    memset(surface, 0, sizeof surface);
    rect.x1 = 3;  rect.y1 = 2;
    rect.x2 = 28; rect.y2 = 9;
    draw_rect(&vwk, &rect, 5);
    show("draw_rect, solid, colour 5, same rectangle");

    /* A rectangle crossing several word boundaries at an odd offset, which is
     * where the left and right end masks have to be right */
    memset(surface, 0, sizeof surface);
    rect.x1 = 5;  rect.y1 = 1;
    rect.x2 = 60; rect.y2 = 3;
    draw_rect(&vwk, &rect, 1);
    show("draw_rect, x 5..60, crossing word boundaries");

    /* A polyline, which goes through the Bresenham code rather than the fill */
    memset(surface, 0, sizeof surface);
    line[0].x = 2;  line[0].y = 2;
    line[1].x = 60; line[1].y = 6;
    line[2].x = 30; line[2].y = 21;
    line[3].x = 2;  line[3].y = 2;
    polyline(&vwk, line, 4, 1);
    show("polyline, a triangle");

    /*
     * An object tree, drawn by EmuTOS's AES object library through EmuTOS's
     * VDI, both built for the host. Nothing in this goes near the emulated
     * machine: it is the proof that the reused AES draws on the ported VDI.
     */
    memset(surface, 0, sizeof surface);
    gsx_init();
    {
        static OBJECT tree[3];

        tree[0].ob_next = -1; tree[0].ob_head = 1; tree[0].ob_tail = 2;
        tree[0].ob_type = G_BOX;
        tree[0].ob_flags = NONE; tree[0].ob_state = OUTLINED;
        tree[0].ob_spec = 0x00021100L;      /* thick border, white */
        tree[0].ob_x = 1; tree[0].ob_y = 1;
        tree[0].ob_width = 60; tree[0].ob_height = 20;

        tree[1].ob_next = 2; tree[1].ob_head = -1; tree[1].ob_tail = -1;
        tree[1].ob_type = G_STRING;
        tree[1].ob_flags = NONE; tree[1].ob_state = NORMAL;
        tree[1].ob_spec = (LONG)(uintptr_t)"GEM";
        tree[1].ob_x = 3; tree[1].ob_y = 3;
        tree[1].ob_width = 24; tree[1].ob_height = 8;

        tree[2].ob_next = 0; tree[2].ob_head = -1; tree[2].ob_tail = -1;
        tree[2].ob_type = G_BUTTON;
        tree[2].ob_flags = LASTOB|SELECTABLE; tree[2].ob_state = NORMAL;
        tree[2].ob_spec = (LONG)(uintptr_t)"OK";
        tree[2].ob_x = 34; tree[2].ob_y = 10;
        tree[2].ob_width = 20; tree[2].ob_height = 9;

        ob_draw(tree, 0, 8);
    }
    show("objc_draw, an object tree from EmuTOS's AES object library");

    /* Text, through v_gtext, which is the entry point an application reaches */
    memset(surface, 0, sizeof surface);
    gtext(2, 14, "Hello TO");
    show("v_gtext, the system font");

    /*
     * The same string in each of the effects. These go through normal_blit,
     * which is the one part of the VDI written here rather than taken from
     * EmuTOS, so this is the only check there is on it.
     */
    memset(surface, 0, sizeof surface);
    vwk.style = F_THICKEN;
    gtext(2, 8, "Bold");
    vwk.style = F_SKEW;
    gtext(2, 20, "Slant");
    vwk.style = 0;
    show("v_gtext, thickened above and skewed below");

    memset(surface, 0, sizeof surface);
    vwk.style = F_LIGHT;
    gtext(2, 14, "Lightened");
    vwk.style = 0;
    show("v_gtext, lightened, which screens the glyph with a mask");

    return 0;
}
