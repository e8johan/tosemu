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
 * Typefaces at any size.
 *
 * The names in the table below are the ones documents of the period were
 * written in. They are Bitstream's, because SpeedoGDOS was Bitstream's: Swiss
 * 721 is their Helvetica, Dutch 801 their Times, Courier 10 Pitch their
 * Courier. Nobody has those files, and a document that names one still has to
 * be set in something - so each is mapped onto whatever the host has that was
 * drawn from the same originals, which for a Linux machine is URW's set.
 *
 * The substitution is visible, and it is meant to be. An application lays a
 * page out from what vqt_extent tells it, so a face whose letters are a
 * fraction wider moves every line break on the page. That happened on real
 * SpeedoGDOS too when a font was missing, and it is not a fault to be hidden -
 * it is the reason the bitmap fonts beside this are worth getting exactly
 * right, those being the ones a document of the period was actually set in.
 *
 * Which faces exist is this table rather than everything fontconfig can find.
 * An application shows the list in a menu and picks out of it by number, and
 * several hundred faces it has never heard of is not what a program written
 * for a machine with four of them expects.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fontface.h"
#include "settings.h"

#ifdef HAVE_FREETYPE

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

/*
 * Where the face numbers start.
 *
 * A .FNT carries its own id and the ones Atari shipped are small - Swiss is 2,
 * Dutch 14, Typewriter 15 - so the outline faces are numbered from well above
 * anything a font file will bring with it. The number itself means nothing to
 * an application: it asks vqt_name for the list and hands back whichever
 * number it was given.
 */
#define FONTFACE_FIRST_ID (5000)

/* How many faces the table may name, which is a bound rather than a target */
#define FONTFACE_MAX (32)

struct face {
    char atari[64];         /* the name a document asks for */
    char host[128];         /* the family fontconfig was asked for */
    char resolved[128];     /* and the family it came back with */
    char file[1024];        /* the file it is in */
    int id;

    FT_Face ft;             /* opened when first drawn with */
    int size64;             /* the size it is currently set to */
};

static struct face faces[FONTFACE_MAX];
static int count;

static FT_Library library;
static int ready;

static int res_x = 72;
static int res_y = 72;

/*
 * The table, and what it is for.
 *
 * Left is what a document asks for, right is the family to ask the host for.
 * A machine without the URW fonts falls through to fontconfig's own idea of
 * sans-serif and the rest, which is what the generic names on the right of the
 * last three lines are for.
 */
static const struct {
    const char *atari;
    const char *host;
} substitutes[] = {
    { "Swiss 721",          "Nimbus Sans"    },
    { "Dutch 801",          "Nimbus Roman"   },
    { "Courier 10 Pitch",   "Nimbus Mono PS" },
    { "Monospace 821",      "Nimbus Mono PS" },
    { "Zapf Humanist",      "Nimbus Sans"    }
};

/*
 * Asking fontconfig for a family and finding out what it actually has.
 *
 * Fontconfig always answers - that is the point of it - so what comes back has
 * to be looked at rather than trusted: asked for a family nobody has, it
 * offers its idea of the nearest thing, and the name it gives back is how that
 * is noticed. Both are kept, because a diagnostic that says Swiss 721 was set
 * in Noto Sans is worth more than one that says a font was found.
 */
static int resolve(const char *family, char *file, int file_size,
                   char *resolved, int resolved_size)
{
    FcPattern *pattern, *matched;
    FcResult result;
    FcChar8 *path = 0, *name = 0;
    int found = 0;

    pattern = FcNameParse((const FcChar8 *)family);
    if (!pattern)
        return 0;

    FcConfigSubstitute(0, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    matched = FcFontMatch(0, pattern, &result);
    if (matched)
    {
        if (FcPatternGetString(matched, FC_FILE, 0, &path) == FcResultMatch
            && path)
        {
            snprintf(file, (size_t)file_size, "%s", (const char *)path);

            if (FcPatternGetString(matched, FC_FAMILY, 0, &name) == FcResultMatch
                && name)
                snprintf(resolved, (size_t)resolved_size, "%s",
                         (const char *)name);
            else
                snprintf(resolved, (size_t)resolved_size, "%s", family);

            found = 1;
        }

        FcPatternDestroy(matched);
    }

    FcPatternDestroy(pattern);

    return found;
}

static void add(const char *atari, const char *host)
{
    struct face *f;
    int i;

    if (count >= FONTFACE_MAX)
        return;

    /* A name said twice is the later line meaning it, the way a settings file
     * works, rather than the same face twice in the menu */
    for (i = 0; i < count; i++)
        if (strcmp(faces[i].atari, atari) == 0)
        {
            f = &faces[i];
            snprintf(f->host, sizeof f->host, "%s", host);
            if (!resolve(host, f->file, (int)sizeof f->file,
                         f->resolved, (int)sizeof f->resolved))
                f->file[0] = 0;
            return;
        }

    f = &faces[count];

    snprintf(f->atari, sizeof f->atari, "%s", atari);
    snprintf(f->host, sizeof f->host, "%s", host);

    if (!resolve(host, f->file, (int)sizeof f->file,
                 f->resolved, (int)sizeof f->resolved))
        return;         /* nothing to draw it with, so it is not offered */

    f->id = FONTFACE_FIRST_ID + count + 1;
    count++;
}

/*
 * A file of substitutions of somebody's own, which is what makes a wrong
 * mapping a line to edit rather than a rebuild. One face to a line, the name a
 * document asks for, an equals sign, and the family to set it in:
 *
 *     Swiss 721 = Helvetica
 *
 * A name already in the table is replaced; one that is not is added.
 */
static void read_substitutes(const char *host_path)
{
    FILE *f = fopen(host_path, "r");
    char line[512];

    if (!f)
    {
        fprintf(stderr, "tosemu: there are no font substitutions at %s\n",
                host_path);
        return;
    }

    while (fgets(line, sizeof line, f))
    {
        char *equals, *atari, *host, *end;

        equals = strchr(line, '=');
        if (!equals || line[0] == ';' || line[0] == '#')
            continue;

        *equals = 0;
        atari = line;
        host = equals + 1;

        while (*atari == ' ' || *atari == '\t')
            atari++;
        for (end = atari + strlen(atari); end > atari && (end[-1] == ' '
             || end[-1] == '\t'); )
            *--end = 0;

        while (*host == ' ' || *host == '\t')
            host++;
        for (end = host + strlen(host); end > host && (end[-1] == '\n'
             || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'); )
            *--end = 0;

        if (*atari && *host)
            add(atari, host);
    }

    fclose(f);
}

int fontface_init(void)
{
    const char *said;
    int i;

    if (ready)
        return count;

    ready = 1;

    if (FT_Init_FreeType(&library) != 0)
    {
        fprintf(stderr, "tosemu: FreeType would not start, so there are no "
                "scalable fonts\n");
        return 0;
    }

    if (!FcInit())
    {
        fprintf(stderr, "tosemu: fontconfig would not start, so there are no "
                "scalable fonts\n");
        return 0;
    }

    for (i = 0; i < (int)(sizeof substitutes / sizeof substitutes[0]); i++)
        add(substitutes[i].atari, substitutes[i].host);

    said = setting("TOSEMU_FONTS_SUBSTITUTES");
    if (said)
        read_substitutes(said);

    return count;
}

int fontface_count(void)
{
    return count;
}

int fontface_id(int index)
{
    return (index >= 0 && index < count) ? faces[index].id : 0;
}

const char *fontface_name(int index)
{
    return (index >= 0 && index < count) ? faces[index].atari : "";
}

const char *fontface_resolved(int index)
{
    return (index >= 0 && index < count) ? faces[index].resolved : "";
}

int fontface_known(int id)
{
    int i;

    for (i = 0; i < count; i++)
        if (faces[i].id == id)
            return 1;

    return 0;
}

void fontface_resolution(int xdpi, int ydpi)
{
    if (xdpi > 0)
        res_x = xdpi;
    if (ydpi > 0)
        res_y = ydpi;

    /* Every face is now set to a size worked out from the old resolution, so
     * none of them may be drawn with until it has been set again */
    for (xdpi = 0; xdpi < count; xdpi++)
        faces[xdpi].size64 = 0;
}

/*
 * The face at a size, opened if it has not been yet.
 *
 * Opening is put off until something is drawn because most runs draw with
 * none of these: a program that never asks for a typeface by name should not
 * pay for four files being parsed.
 */
static struct face *at_size(int id, int size64)
{
    struct face *f = 0;
    int i;

    for (i = 0; i < count; i++)
        if (faces[i].id == id)
            f = &faces[i];

    if (!f || size64 <= 0)
        return 0;

    if (!f->ft && FT_New_Face(library, f->file, 0, &f->ft) != 0)
    {
        fprintf(stderr, "tosemu: %s is set in %s, and %s could not be read\n",
                f->atari, f->resolved, f->file);
        f->ft = 0;
        return 0;
    }

    if (f->size64 != size64)
    {
        if (FT_Set_Char_Size(f->ft, 0, size64, (FT_UInt)res_x,
                             (FT_UInt)res_y) != 0)
            return 0;

        f->size64 = size64;
    }

    return f;
}

/* An Atari character to a Unicode one. The first hundred and twenty eight are
 * ASCII and are the ones a document is mostly made of; above that the Atari
 * set is its own and nothing here maps it yet. */
static unsigned long unicode_of(int character)
{
    return (unsigned long)(character & 0xff);
}

int fontface_metrics(int id, int size64, struct fontface_metrics *into)
{
    struct face *f = at_size(id, size64);

    if (!f || !into)
        return 0;

    /* FreeType keeps these in 26.6 fixed point once a size is set, so they
     * come down to whole pixels by rounding up - a character that needs a
     * fraction of a pixel needs the pixel */
    into->top = (int)((f->ft->size->metrics.ascender + 63) >> 6);
    into->ascent = into->top;
    into->descent = (int)((-f->ft->size->metrics.descender + 63) >> 6);
    into->bottom = into->descent;
    into->max_width = (int)((f->ft->size->metrics.max_advance + 63) >> 6);

    /* The height of a lower case x, which is what half means and what a font
     * header carries. Asking the glyph is the only way to know it. */
    into->half = into->ascent / 2;
    if (FT_Load_Char(f->ft, 'x', FT_LOAD_DEFAULT) == 0)
        into->half = (int)((f->ft->glyph->metrics.height + 63) >> 6);

    into->first = 32;
    into->last = 255;

    return 1;
}

int fontface_advance(int id, int size64, int character)
{
    struct face *f = at_size(id, size64);

    if (!f)
        return 0;

    if (FT_Load_Char(f->ft, unicode_of(character), FT_LOAD_DEFAULT) != 0)
        return 0;

    return (int)((f->ft->glyph->advance.x + 63) >> 6);
}

int fontface_extent(int id, int size64, const char *text, int length,
                    int *width, int *height)
{
    struct face *f = at_size(id, size64);
    struct fontface_metrics m;
    long pen = 0;
    int i;

    if (!f)
        return 0;

    for (i = 0; i < length; i++)
    {
        if (FT_Load_Char(f->ft, unicode_of((unsigned char)text[i]),
                         FT_LOAD_DEFAULT) != 0)
            continue;

        pen += f->ft->glyph->advance.x;
    }

    if (width)
        *width = (int)((pen + 63) >> 6);

    if (height && fontface_metrics(id, size64, &m))
        *height = m.top + m.descent;

    return 1;
}

/* One bit set in a bitmap whose rows are whole words */
static void plot(struct fontface_bitmap *b, int x, int y)
{
    if (x < 0 || y < 0 || x >= b->width || y >= b->height)
        return;

    b->bits[y * b->words + (x >> 4)] |= (unsigned short)(0x8000 >> (x & 15));
}

int fontface_render(int id, int size64, const char *text, int length,
                    struct fontface_bitmap *into)
{
    struct face *f = at_size(id, size64);
    struct fontface_metrics m;
    long pen = 0;
    int i, width = 0, height;

    if (!f || !into)
        return 0;

    if (!fontface_metrics(id, size64, &m))
        return 0;

    if (!fontface_extent(id, size64, text, length, &width, &height))
        return 0;

    /*
     * The bitmap is the cell the string occupies and no more: as wide as the
     * advances add up to, and as tall as the face is from its highest ink to
     * its lowest.
     *
     * It is worth saying why it is not larger. A character can lean past its
     * own advance - an italic f, or a letter whose ink starts left of its
     * origin - and giving the bitmap a character's slack at each end so that
     * such ink survives makes drawing wrong in a way that is much worse than
     * losing it. The blit puts the whole rectangle down, and in replace mode
     * that means writing the background over the slack: a word drawn next to
     * one already on the screen erases its neighbour's edge. It shows while
     * somebody is typing, where each keystroke redraws one run, and not on a
     * full redraw, where everything is painted left to right and the damage is
     * covered by what comes after it.
     *
     * A cell is also what the VDI does with a font from a file, so a string
     * that leans is clipped here the same way it is clipped there.
     */
    into->width = width;
    into->height = m.top + m.descent;
    into->words = (into->width + 15) / 16;
    into->origin_x = 0;
    into->origin_y = m.top;

    if (into->width <= 0 || into->height <= 0)
        return 0;

    into->bits = calloc((size_t)(into->words * into->height),
                        sizeof *into->bits);
    if (!into->bits)
        return 0;

    for (i = 0; i < length; i++)
    {
        FT_Bitmap *bitmap;
        int row, column, left, top;

        if (FT_Load_Char(f->ft, unicode_of((unsigned char)text[i]),
                         FT_LOAD_RENDER | FT_LOAD_MONOCHROME) != 0)
            continue;

        bitmap = &f->ft->glyph->bitmap;
        left = into->origin_x + (int)((pen + 32) >> 6) + f->ft->glyph->bitmap_left;
        top = into->origin_y - f->ft->glyph->bitmap_top;

        for (row = 0; row < (int)bitmap->rows; row++)
            for (column = 0; column < (int)bitmap->width; column++)
            {
                const unsigned char *line =
                    bitmap->buffer + (long)row * bitmap->pitch;

                if (line[column >> 3] & (0x80 >> (column & 7)))
                    plot(into, left + column, top + row);
            }

        pen += f->ft->glyph->advance.x;
    }

    return 1;
}

void fontface_render_free(struct fontface_bitmap *bitmap)
{
    if (!bitmap)
        return;

    free(bitmap->bits);
    bitmap->bits = 0;
}

#else /* HAVE_FREETYPE */

/*
 * Built without FreeType, which is a build somebody chose rather than a
 * failure: the fonts on the disk still work and vq_gdos answers FontGDOS
 * rather than SpeedoGDOS. Saying there are no faces is the whole of it, and
 * everything above this refuses politely once nothing is in the list.
 */

int fontface_init(void)
{
    return 0;
}

int fontface_count(void)
{
    return 0;
}

int fontface_id(int index)
{
    (void)index;
    return 0;
}

const char *fontface_name(int index)
{
    (void)index;
    return "";
}

const char *fontface_resolved(int index)
{
    (void)index;
    return "";
}

int fontface_known(int id)
{
    (void)id;
    return 0;
}

void fontface_resolution(int xdpi, int ydpi)
{
    (void)xdpi;
    (void)ydpi;
}

int fontface_metrics(int id, int size64, struct fontface_metrics *into)
{
    (void)id; (void)size64; (void)into;
    return 0;
}

int fontface_extent(int id, int size64, const char *text, int length,
                    int *width, int *height)
{
    (void)id; (void)size64; (void)text; (void)length; (void)width; (void)height;
    return 0;
}

int fontface_advance(int id, int size64, int character)
{
    (void)id; (void)size64; (void)character;
    return 0;
}

int fontface_render(int id, int size64, const char *text, int length,
                    struct fontface_bitmap *into)
{
    (void)id; (void)size64; (void)text; (void)length; (void)into;
    return 0;
}

void fontface_render_free(struct fontface_bitmap *bitmap)
{
    (void)bitmap;
}

#endif /* HAVE_FREETYPE */
