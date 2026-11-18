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
 * Colour icons in a resource file, which are not where the rest of it is.
 *
 * TOS 4 put them after the end of the file as an older AES measures it, and
 * said so with a bit in rsh_vrsn and an array of longs at rsh_rssize: how long
 * the file really is and where the icons are. And a G_CICON's ob_spec is the
 * icon's number in a table rather than an offset into the file. An AES that
 * knows none of this reads up to rsh_rssize, adds the number to the start of
 * the file and hands the application a pointer into the header.
 *
 * There is no resource editor here to make a file with, so the test writes
 * one. It has two icons, and the object that is drawn is the second, because
 * nothing in the file says where the second begins: it is found by measuring
 * the first, all of it. So the first is the awkward one - two colour versions
 * chained by next_res, the first of them with a selected state as well - and
 * a mistake in measuring any part of it puts the second somewhere else.
 *
 * What is drawn of a colour icon is its mono form, the same as a G_ICON, so
 * the pixels are checked the way c-icon checks them and on the same screen,
 * the ST's low resolution one with colours enough to tell the mask from the
 * image.
 *
 * A test that stops the emulator prints nothing further, so the count at the
 * end is what says the whole file ran.
 */

#include <stdio.h>
#include <string.h>
#include <gem.h>

#define FILENAME    "CICON.RSC"

/* Colours as the VDI numbers them, which is what v_get_pixel answers with */
#define WHITE       (0)
#define BLACK       (1)
#define RED         (2)

/* The objects in the one tree */
#define FIRST       (1)
#define SECOND      (2)

/* Where they are in characters, which is how a file says it; eight pixels
 * each way on the low resolution screen */
#define BOX_X       (5)
#define BOX_Y       (5)
#define BOX_W       (16)
#define BOX_H       (12)
#define FIRST_X     (8)
#define SECOND_X    (1)
#define ICON_Y      (3)

/* And where the second lands, which is what the pixels are read at */
#define ICO_SX      ((BOX_X + SECOND_X) * 8)
#define ICO_SY      ((BOX_Y + ICON_Y) * 8)
#define TXT_SX      (ICO_SX)
#define TXT_SY      (ICO_SY + 8)
#define TXT_W       (32)
#define TXT_H       (8)

/* Sixteen by eight, so each form is eight words */
#define ICON_W      (16)
#define ICON_H      (8)
#define WORDS       (ICON_W / 16 * ICON_H)

/* What the forms of the second icon start with, to recognise them by */
#define COLOUR_WORD (0x1234)
#define CMASK_WORD  (0x7ffe)

static unsigned char file[1024];
static unsigned long at;

static int n;

static void check(long got, long want, const char *name)
{
    n++;
    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
}

/* The file is written a big endian word at a time, as the machine keeps it */
static void put16_at(unsigned long where, unsigned short value)
{
    file[where] = (unsigned char)(value >> 8);
    file[where + 1] = (unsigned char)value;
}

static void put32_at(unsigned long where, unsigned long value)
{
    put16_at(where, (unsigned short)(value >> 16));
    put16_at(where + 2, (unsigned short)value);
}

static void put16(unsigned short value)
{
    put16_at(at, value);
    at += 2;
}

static void put32(unsigned long value)
{
    put32_at(at, value);
    at += 4;
}

/* A form of WORDS words, the first of them given and the rest one value */
static void form(unsigned short first, unsigned short rest, int planes)
{
    int i;

    for (i = 0; i < WORDS * planes; i++)
        put16(i == 0 ? first : rest);
}

static void object(short next, short head, short tail, unsigned short type,
                   unsigned short flags, unsigned long spec,
                   short x, short y, short w, short h)
{
    put16(next);
    put16(head);
    put16(tail);
    put16(type);
    put16(flags);
    put16(0);           /* ob_state */
    put32(spec);
    put16(x);
    put16(y);
    put16(w);
    put16(h);
}

/* A CICONBLK as the file has it: an ICONBLK whose pointers are filled in by
 * the AES, then how many colour versions follow, then the mono form, its mask
 * and twelve bytes of text */
static void ciconblk(unsigned long versions, unsigned short first,
                     const char *text)
{
    char label[12];

    put32(0);           /* ib_pmask */
    put32(0);           /* ib_pdata */
    put32(0);           /* ib_ptext */
    put16(0x1200);      /* black on red, and no character */
    put16(0);           /* ib_xchar */
    put16(0);           /* ib_ychar */
    put16(0);           /* ib_xicon */
    put16(0);           /* ib_yicon */
    put16(ICON_W);
    put16(ICON_H);
    put16(0);           /* ib_xtext */
    put16(ICON_H);      /* ib_ytext, the label going under the icon */
    put16(TXT_W);
    put16(TXT_H);
    put32(versions);

    form(first, 0x0000, 1);             /* the image */
    form(0xffff, 0xffff, 1);            /* and a mask that covers all of it */

    memset(label, 0, sizeof label);
    strncpy(label, text, sizeof label - 1);
    memcpy(file + at, label, sizeof label);
    at += sizeof label;
}

/* A CICON as the file has it, followed by its forms. In the file sel_data is
 * only whether there is a selected state and next_res whether another
 * version follows this one */
static void cicon(int planes, int selected, int more,
                  unsigned short colour, unsigned short mask)
{
    put16((unsigned short)planes);
    put32(0);                           /* col_data */
    put32(0);                           /* col_mask */
    put32(selected ? 1 : 0);
    put32(0);                           /* sel_mask */
    put32(more ? 1 : 0);

    form(colour, 0x0000, planes);
    form(mask, 0xffff, 1);
    if (selected)
    {
        form(0x5555, 0x0000, planes);
        form(0xaaaa, 0xffff, 1);
    }
}

static int write_resource(void)
{
    unsigned long objects, trindex, rssize, table, size;
    FILE *f;

    /* The header is filled in last, when everything it points at is placed */
    at = 36;

    objects = at;
    object(-1, FIRST, SECOND, G_BOX, 0, 0x00021100L,
           BOX_X, BOX_Y, BOX_W, BOX_H);
    object(SECOND, -1, -1, G_CICON, 0, 0, FIRST_X, ICON_Y, 4, 2);
    object(0, -1, -1, G_CICON, 0x0020, 1, SECOND_X, ICON_Y, 4, 2);

    trindex = at;
    put32(objects);

    /* Which is where an AES that knows nothing of colour icons stops */
    rssize = at;
    put32(0);                           /* the length, once it is known */
    put32(0);                           /* where the table is */
    put32(0);                           /* the end of the array */

    table = at;
    put32(0);
    put32(0);
    put32(0xffffffffUL);

    /* The first icon: four planes with a selected state, then one plane */
    ciconblk(2, 0x0001, "First");
    cicon(4, 1, 1, 0x4444, 0xffff);
    cicon(1, 0, 0, 0x1111, 0xffff);

    /* The second, which is the one drawn */
    ciconblk(1, 0x8001, "Icon");
    cicon(4, 0, 0, COLOUR_WORD, CMASK_WORD);

    size = at;

    put32_at(rssize, size);
    put32_at(rssize + 4, table);

    put16_at(0, 0x0004);                /* rsh_vrsn: the new format */
    put16_at(2, (unsigned short)objects);
    put16_at(4, (unsigned short)trindex);    /* no TEDINFOs, ICONBLKs, */
    put16_at(6, (unsigned short)trindex);    /* BITBLKs, strings or images, */
    put16_at(8, (unsigned short)trindex);    /* so they are all said to be */
    put16_at(10, (unsigned short)trindex);   /* where there is nothing */
    put16_at(12, (unsigned short)trindex);
    put16_at(14, (unsigned short)trindex);
    put16_at(16, (unsigned short)trindex);
    put16_at(18, (unsigned short)trindex);
    put16_at(20, 3);                    /* rsh_nobs */
    put16_at(22, 1);                    /* rsh_ntree */
    put16_at(34, (unsigned short)rssize);

    f = fopen(FILENAME, "wb");
    if (!f)
        return 0;
    if (fwrite(file, 1, size, f) != size)
    {
        fclose(f);
        return 0;
    }

    return fclose(f) == 0;
}

static short handle;

static short pixel(short x, short y)
{
    short pel, index;

    v_get_pixel(handle, x, y, &pel, &index);

    return index;
}

static int any(short x, short y, short w, short h, short colour)
{
    short i, j;

    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (pixel(i, j) == colour)
                return 1;

    return 0;
}

int main(int argc, char **argv)
{
    short wchar, hchar, wbox, hbox;
    OBJECT *tree = NULL;
    CICONBLK *first, *second;
    CICON *version;

    if (appl_init() < 0)
    {
        printf("Bail out! - no AES to talk to\n");
        return 1;
    }

    handle = graf_handle(&wchar, &hchar, &wbox, &hbox);

    if (!write_resource())
    {
        printf("Bail out! - could not write %s\n", FILENAME);
        appl_exit();
        return 1;
    }

    check(rsrc_load(FILENAME) != 0, 1, "a resource with colour icons loads");
    rsrc_gaddr(R_TREE, 0, &tree);
    check(tree != NULL, 1, "and its tree is there");
    if (!tree)
    {
        printf("Bail out! - no tree\n");
        appl_exit();
        return 1;
    }

    /*
     * The second icon, which is only where it should be if the whole of the
     * first was measured.
     */
    second = (CICONBLK *)tree[SECOND].ob_spec.index;
    check(strcmp(second->monoblk.ib_ptext, "Icon"), 0,
          "a G_CICON's ob_spec is the icon of that number, past the first");
    check(second->monoblk.ib_pdata[0], (short)0x8001,
          "its mono image is the one in the file");
    check(second->monoblk.ib_pmask[0], (short)0xffff, "and so is its mask");

    version = second->mainlist;
    check(version != NULL, 1, "its colour version is in the list");
    if (version)
    {
        check(version->num_planes, 4, "with four planes");
        check(version->col_data[0], COLOUR_WORD,
              "whose image is after the CICON");
        check(version->col_mask[0], CMASK_WORD, "and whose mask after that");
        check(version->sel_data == NULL, 1,
              "and no selected state, since it has none");
        check(version->next_res == NULL, 1, "and nothing after it");
    }

    /*
     * The first, whose versions are a chain and whose first version has a
     * selected state of its own.
     */
    first = (CICONBLK *)tree[FIRST].ob_spec.index;
    check(strcmp(first->monoblk.ib_ptext, "First"), 0,
          "the first icon is number nought");
    version = first->mainlist;
    check(version != NULL && version->num_planes == 4, 1,
          "its first version is the four plane one");
    if (version)
    {
        check(version->sel_data != NULL && version->sel_data[0] == 0x5555, 1,
              "with its selected image after its mask");
        check(version->sel_mask != NULL
              && version->sel_mask[0] == (short)0xaaaa, 1,
              "and the selected mask after that");
        version = version->next_res;
        check(version != NULL && version->num_planes == 1, 1,
              "next_res leads to the one plane version");
        if (version)
            check(version->col_data[0], 0x1111,
                  "past all four forms of the one before");
    }

    /* And drawn, which is its mono form, as for a G_ICON */
    objc_draw(tree, ROOT, 8, BOX_X * 8, BOX_Y * 8, BOX_W * 8, BOX_H * 8);

    check(pixel(ICO_SX, ICO_SY), BLACK, "the colour icon is drawn");
    check(pixel(ICO_SX + 15, ICO_SY), BLACK, "out to the last bit of the word");
    check(pixel(ICO_SX + 8, ICO_SY), RED, "over its mask");
    check(any(TXT_SX, TXT_SY, TXT_W, TXT_H, BLACK), 1,
          "and with its label under it");

    rsrc_free();
    remove(FILENAME);

    printf("1..%d\n", n);

    appl_exit();

    return 0;
}
