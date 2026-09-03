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
 * GEM Raster, which is what an application means by SCRAP.IMG.
 *
 * The header is eight big endian words - a version, its own length, how many
 * planes, how long a pattern is, how large a pixel is in millionths of a metre,
 * and then the width and height. After it come the rows, each plane of a row in
 * turn, run encoded in four ways:
 *
 *   00 00 ff nn   the row that follows is written nn times
 *   00 nn ...     the pattern of `pattern length' bytes after it, nn times
 *   80 nn ...     the nn bytes after it, as they are
 *   bb           a run of bb & 0x7f bytes, of 0xff if bit 7 is set else 0x00
 *
 * Every one of those was read off a file rather than remembered: netpbm's
 * pbmtogem writes all four given the right picture, and what is written here
 * was checked by decoding what it produced and by handing what this produces
 * back to gemtopbm. The counts are the part worth being sure about - the solid
 * run counts bytes and not pixels, which is a difference of eight and is the
 * sort of thing that produces a picture stretched into stripes rather than an
 * obvious failure.
 */

#include "scrapimg.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_PNG
#include <png.h>
#endif

/* The header, in words */
#define IMG_VERSION   (0)
#define IMG_HEADER    (1)
#define IMG_PLANES    (2)
#define IMG_PATTERN   (3)
#define IMG_PIXEL_W   (4)
#define IMG_PIXEL_H   (5)
#define IMG_WIDTH     (6)
#define IMG_HEIGHT    (7)

#define IMG_WORDS     (8)

/*
 * How large a pixel is, in millionths of a metre.
 *
 * The ST's high resolution screen, which is the one GEM applications were drawn
 * for and the only one whose pixels are square. Nothing on a desktop reads it,
 * but an application on the other side of the scrap might, and a picture that
 * says nothing about its shape is one that gets drawn at whatever shape the
 * reader assumes.
 */
#define PIXEL_MICRONS (372)

/* Nothing sensible is larger, and a header saying otherwise is a header to
 * stop reading rather than to allocate from */
#define MAX_SIDE      (4096)
#define MAX_PLANES    (8)

int scrap_img_available(void)
{
#ifdef HAVE_PNG
    return 1;
#else
    return 0;
#endif
}

#ifdef HAVE_PNG

/* A word of the header, which is big endian however this machine is */
static unsigned word_at(const unsigned char *at)
{
    return (unsigned)at[0] << 8 | at[1];
}

static void put_word(unsigned char *at, unsigned value)
{
    at[0] = (unsigned char)(value >> 8);
    at[1] = (unsigned char)(value & 0xff);
}

/*
 * Undoes the run encoding of one row of one plane.
 *
 * Answers how many bytes of the encoded data were used, or 0 for anything that
 * does not decode - a count that runs off the end of the row, or off the end of
 * the file. Both are what a truncated or a malformed picture looks like, and
 * both have to be refused rather than trusted, because what is being decoded
 * came out of a file this program did not write.
 */
static size_t row_decode(const unsigned char *from, size_t left,
                         unsigned char *row, size_t row_bytes,
                         unsigned pattern)
{
    size_t used = 0;
    size_t at = 0;

    while (at < row_bytes)
    {
        unsigned char lead;

        if (used >= left)
            return 0;

        lead = from[used];

        if (lead == 0x80)
        {
            /* So many bytes, as they are */
            size_t n;

            if (used + 2 > left)
                return 0;

            n = from[used + 1];

            if (n == 0 || at + n > row_bytes || used + 2 + n > left)
                return 0;

            memcpy(row + at, from + used + 2, n);
            at += n;
            used += 2 + n;
        }
        else if (lead == 0x00)
        {
            /* A pattern, so many times */
            size_t times, i, j;

            if (used + 2 > left)
                return 0;

            times = from[used + 1];

            if (times == 0)
                return 0;   /* the vertical run, which is not a row's to use */

            if (used + 2 + pattern > left)
                return 0;

            if (at + times * pattern > row_bytes)
                return 0;

            for (i = 0; i < times; i++)
                for (j = 0; j < pattern; j++)
                    row[at++] = from[used + 2 + j];

            used += 2 + pattern;
        }
        else
        {
            /* A run of one colour, counted in bytes rather than in pixels */
            size_t n = lead & 0x7f;
            unsigned char with = (lead & 0x80) ? 0xff : 0x00;

            if (n == 0 || at + n > row_bytes)
                return 0;

            memset(row + at, with, n);
            at += n;
            used += 1;
        }
    }

    return used;
}

/*
 * The whole picture, as one colour index per pixel.
 *
 * Allocates width * height bytes. The planes are gathered here rather than left
 * as planes because everything downstream wants a colour and not a bit: an
 * index is what the palette is looked up by, and it is what a plane count of
 * one or four stops mattering at.
 */
static unsigned char *img_pixels(const void *img, size_t img_length,
                                 unsigned *width, unsigned *height,
                                 unsigned *planes)
{
    const unsigned char *bytes = img;
    const unsigned char *data;
    unsigned char *out;
    unsigned char *row;
    size_t left, row_bytes;
    unsigned w, h, p, pattern, header;
    unsigned y;

    if (img_length < IMG_WORDS * 2)
        return 0;

    if (word_at(bytes + IMG_VERSION * 2) != 1)
        return 0;

    header = word_at(bytes + IMG_HEADER * 2);
    p = word_at(bytes + IMG_PLANES * 2);
    pattern = word_at(bytes + IMG_PATTERN * 2);
    w = word_at(bytes + IMG_WIDTH * 2);
    h = word_at(bytes + IMG_HEIGHT * 2);

    if (header < IMG_WORDS || (size_t)header * 2 > img_length)
        return 0;

    if (!w || !h || w > MAX_SIDE || h > MAX_SIDE)
        return 0;

    if (!p || p > MAX_PLANES || !pattern || pattern > 16)
        return 0;

    data = bytes + header * 2;
    left = img_length - (size_t)header * 2;

    row_bytes = (w + 7) / 8;

    out = calloc((size_t)w * h, 1);
    row = malloc(row_bytes);

    if (!out || !row)
    {
        free(out);
        free(row);
        return 0;
    }

    for (y = 0; y < h; )
    {
        unsigned times = 1;
        unsigned plane, again;

        /*
         * A row that is written more than once says so before itself, which is
         * the one thing here that is not per plane: it repeats the whole row,
         * every plane of it.
         */
        if (left >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0xff)
        {
            times = data[3];

            if (!times)
                break;

            data += 4;
            left -= 4;
        }

        for (plane = 0; plane < p; plane++)
        {
            size_t used = row_decode(data, left, row, row_bytes, pattern);
            unsigned x;

            if (!used)
            {
                /*
                 * A picture that stops in the middle of itself, which is what a
                 * truncated file looks like. What was decoded before that is
                 * still a picture and is worth more than nothing, so the size
                 * is reported and the rest is left as it was cleared - but it
                 * has to be reported here as well as at the end, or the caller
                 * is handed pixels and no shape to read them in.
                 */
                free(row);

                *width = w;
                *height = h;
                *planes = p;

                return out;
            }

            data += used;
            left -= used;

            for (x = 0; x < w; x++)
                if (row[x >> 3] & (0x80 >> (x & 7)))
                    out[(size_t)y * w + x] |= (unsigned char)(1u << plane);
        }

        /* And now that row, as many times as it said */
        for (again = 1; again < times && y + again < h; again++)
            memcpy(out + (size_t)(y + again) * w, out + (size_t)y * w, w);

        y += times;
    }

    free(row);

    *width = w;
    *height = h;
    *planes = p;

    return out;
}

/* What a colour index looks like. One plane is black and white said outright
 * rather than through the palette, which is what a mono picture means. */
static void colour_of(unsigned index, const uint32_t *palette, int colours,
                      unsigned planes, unsigned char *rgb)
{
    uint32_t argb;

    if (planes == 1)
    {
        /* A set bit is black, which is what every GEM tool that writes one
         * plane means and what a PBM means by the same bit */
        rgb[0] = rgb[1] = rgb[2] = index ? 0 : 0xff;
        return;
    }

    argb = (palette && (int)index < colours) ? palette[index] : 0;

    rgb[0] = (unsigned char)(argb >> 16);
    rgb[1] = (unsigned char)(argb >> 8);
    rgb[2] = (unsigned char)argb;
}

int scrap_img_to_png(const void *img, size_t img_length,
                     const uint32_t *palette, int colours,
                     void **png, size_t *png_length)
{
    png_image writing;
    unsigned char *pixels;
    unsigned char *rgb;
    unsigned w = 0, h = 0, p = 0;
    png_alloc_size_t room;
    void *out;
    size_t i, n;

    pixels = img_pixels(img, img_length, &w, &h, &p);
    if (!pixels)
        return 0;

    n = (size_t)w * h;

    rgb = malloc(n * 3);
    if (!rgb)
    {
        free(pixels);
        return 0;
    }

    for (i = 0; i < n; i++)
        colour_of(pixels[i], palette, colours, p, rgb + i * 3);

    free(pixels);

    memset(&writing, 0, sizeof writing);
    writing.version = PNG_IMAGE_VERSION;
    writing.width = w;
    writing.height = h;
    writing.format = PNG_FORMAT_RGB;

    /* Asked for rather than guessed. A picture of flat colours compresses to
     * almost nothing and one of noise to more than it started as. */
    room = 0;
    if (!png_image_write_to_memory(&writing, 0, &room, 0, rgb, 0, 0))
    {
        free(rgb);
        return 0;
    }

    out = malloc(room ? (size_t)room : 1);
    if (!out)
    {
        free(rgb);
        return 0;
    }

    if (!png_image_write_to_memory(&writing, out, &room, 0, rgb, 0, 0))
    {
        free(rgb);
        free(out);
        return 0;
    }

    free(rgb);

    *png = out;
    *png_length = (size_t)room;

    return 1;
}

/* How far apart two colours are, near enough for choosing between sixteen of
 * them. The squares rather than the roots, there being nothing to compare them
 * against but each other. */
static long apart(long r, long g, long b, uint32_t argb)
{
    long dr = r - (long)((argb >> 16) & 0xff);
    long dg = g - (long)((argb >> 8) & 0xff);
    long db = b - (long)(argb & 0xff);

    return dr * dr + dg * dg + db * db;
}

/*
 * Which of the machine's colours a pixel becomes.
 *
 * Nearest wins, and for one plane it is a question of light or dark rather than
 * of colour - a mono picture has no palette to be near to.
 */
static unsigned nearest(long r, long g, long b, const uint32_t *palette,
                        int colours, unsigned planes, unsigned char *chosen)
{
    unsigned best = 0;
    long best_apart = 0;
    int i;

    if (planes == 1)
    {
        best = (r * 299 + g * 587 + b * 114) / 1000 < 128 ? 1u : 0u;

        chosen[0] = chosen[1] = chosen[2] = best ? 0 : 0xff;

        return best;
    }

    for (i = 0; i < colours; i++)
    {
        long how = apart(r, g, b, palette[i]);

        if (i == 0 || how < best_apart)
        {
            best_apart = how;
            best = (unsigned)i;
        }
    }

    chosen[0] = (unsigned char)((palette[best] >> 16) & 0xff);
    chosen[1] = (unsigned char)((palette[best] >> 8) & 0xff);
    chosen[2] = (unsigned char)(palette[best] & 0xff);

    return best;
}

/* Somewhere to put an encoded row, and how much of it is used */
struct out {
    unsigned char *bytes;
    size_t used;
    size_t room;
};

static int out_byte(struct out *o, unsigned char b)
{
    if (o->used == o->room)
    {
        size_t bigger = o->room ? o->room * 2 : 1024;
        unsigned char *more = realloc(o->bytes, bigger);

        if (!more)
            return 0;

        o->bytes = more;
        o->room = bigger;
    }

    o->bytes[o->used++] = b;

    return 1;
}

/*
 * Run encodes one row of one plane.
 *
 * Solid runs where there are any and literal runs for everything else, which is
 * every form a reader has to understand except the pattern - and a pattern run
 * saves nothing a solid run does not on the pictures a GEM application makes.
 * Both counts are a byte, so both are broken into pieces of 127 or fewer.
 */
static int row_encode(struct out *o, const unsigned char *row, size_t row_bytes)
{
    size_t at = 0;

    while (at < row_bytes)
    {
        size_t run = 1;
        unsigned char here = row[at];

        while (at + run < row_bytes && row[at + run] == here && run < 127)
            run++;

        /* Three of a kind is where a solid run starts paying: two bytes to say
         * it against two to say a literal of one */
        if ((here == 0x00 || here == 0xff) && run >= 2)
        {
            if (!out_byte(o, (unsigned char)((here ? 0x80 : 0x00) | run)))
                return 0;

            at += run;
        }
        else
        {
            /* Everything up to the next solid run worth having */
            size_t start = at;
            size_t n;

            while (at < row_bytes)
            {
                size_t same = 1;

                while (at + same < row_bytes && row[at + same] == row[at]
                       && same < 127)
                    same++;

                if ((row[at] == 0x00 || row[at] == 0xff) && same >= 2)
                    break;

                at += same;

                if (at - start >= 127)
                    break;
            }

            n = at - start;

            if (n > 127)
            {
                at = start + 127;
                n = 127;
            }

            if (!out_byte(o, 0x80) || !out_byte(o, (unsigned char)n))
                return 0;

            for (; start < at; start++)
                if (!out_byte(o, row[start]))
                    return 0;
        }
    }

    return 1;
}

int scrap_img_from_png(const void *png, size_t png_length,
                       const uint32_t *palette, int colours, int planes,
                       void **img, size_t *img_length)
{
    png_image reading;
    unsigned char *rgb = 0;
    long *carry = 0;
    unsigned char *row = 0;
    struct out o;
    unsigned char header[IMG_WORDS * 2];
    unsigned w, h, y;
    size_t row_bytes;
    unsigned char *out;

    memset(&o, 0, sizeof o);

    if (planes < 1 || planes > MAX_PLANES)
        return 0;

    memset(&reading, 0, sizeof reading);
    reading.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&reading, png, png_length))
        return 0;

    reading.format = PNG_FORMAT_RGB;

    w = reading.width;
    h = reading.height;

    if (!w || !h || w > MAX_SIDE || h > MAX_SIDE)
    {
        png_image_free(&reading);
        return 0;
    }

    rgb = malloc(PNG_IMAGE_SIZE(reading));
    if (!rgb)
    {
        png_image_free(&reading);
        return 0;
    }

    if (!png_image_finish_read(&reading, 0, rgb, 0, 0))
    {
        free(rgb);
        return 0;
    }

    row_bytes = (w + 7) / 8;

    /*
     * What each pixel could not be given, to be handed to the ones after it.
     *
     * Three colours for this row and the next, because the error goes to the
     * right and downwards - which is what keeps a photograph from arriving as
     * a handful of flat blocks. Held as longs because it goes negative and
     * because it is added to before it is clamped.
     */
    carry = calloc((size_t)(w + 2) * 3 * 2, sizeof *carry);
    row = malloc(row_bytes);

    if (!carry || !row)
    {
        free(rgb);
        free(carry);
        free(row);
        return 0;
    }

    for (y = 0; y < h; y++)
    {
        long *this_row = carry + (size_t)(y & 1) * (w + 2) * 3;
        long *next_row = carry + (size_t)((y + 1) & 1) * (w + 2) * 3;
        int plane;
        unsigned x;
        unsigned char *index = row;     /* used below, one index per pixel */
        unsigned char *indices;

        indices = malloc(w);
        if (!indices)
            break;

        memset(next_row, 0, (size_t)(w + 2) * 3 * sizeof *next_row);

        for (x = 0; x < w; x++)
        {
            const unsigned char *at = rgb + ((size_t)y * w + x) * 3;
            unsigned char chosen[3];
            long want[3];
            int c;

            for (c = 0; c < 3; c++)
            {
                want[c] = at[c] + this_row[(x + 1) * 3 + c];

                if (want[c] < 0)
                    want[c] = 0;
                if (want[c] > 255)
                    want[c] = 255;
            }

            indices[x] = (unsigned char)nearest(want[0], want[1], want[2],
                                                palette, colours,
                                                (unsigned)planes, chosen);

            /*
             * Floyd and Steinberg's shares: seven sixteenths to the right, and
             * three, five and one to the row below. Nothing clever, and the
             * reason it is here rather than a plain nearest colour is that
             * sixteen colours out of sixteen million is a large enough step to
             * see as banding when nothing carries the difference along.
             */
            for (c = 0; c < 3; c++)
            {
                long err = want[c] - chosen[c];

                this_row[(x + 2) * 3 + c] += err * 7 / 16;
                next_row[(x + 0) * 3 + c] += err * 3 / 16;
                next_row[(x + 1) * 3 + c] += err * 5 / 16;
                next_row[(x + 2) * 3 + c] += err * 1 / 16;
            }
        }

        (void)index;

        for (plane = 0; plane < planes; plane++)
        {
            memset(row, 0, row_bytes);

            for (x = 0; x < w; x++)
                if (indices[x] & (1u << plane))
                    row[x >> 3] |= (unsigned char)(0x80 >> (x & 7));

            if (!row_encode(&o, row, row_bytes))
            {
                free(indices);
                free(rgb);
                free(carry);
                free(row);
                free(o.bytes);
                return 0;
            }
        }

        free(indices);
    }

    free(rgb);
    free(carry);
    free(row);

    put_word(header + IMG_VERSION * 2, 1);
    put_word(header + IMG_HEADER * 2, IMG_WORDS);
    put_word(header + IMG_PLANES * 2, (unsigned)planes);
    put_word(header + IMG_PATTERN * 2, 1);
    put_word(header + IMG_PIXEL_W * 2, PIXEL_MICRONS);
    put_word(header + IMG_PIXEL_H * 2, PIXEL_MICRONS);
    put_word(header + IMG_WIDTH * 2, w);
    put_word(header + IMG_HEIGHT * 2, h);

    out = malloc(sizeof header + o.used);
    if (!out)
    {
        free(o.bytes);
        return 0;
    }

    memcpy(out, header, sizeof header);
    memcpy(out + sizeof header, o.bytes, o.used);

    free(o.bytes);

    *img = out;
    *img_length = sizeof header + o.used;

    return 1;
}

#else /* HAVE_PNG */

int scrap_img_to_png(const void *img, size_t img_length,
                     const uint32_t *palette, int colours,
                     void **png, size_t *png_length)
{
    (void)img; (void)img_length; (void)palette; (void)colours;
    (void)png; (void)png_length;

    return 0;
}

int scrap_img_from_png(const void *png, size_t png_length,
                       const uint32_t *palette, int colours, int planes,
                       void **img, size_t *img_length)
{
    (void)png; (void)png_length; (void)palette; (void)colours; (void)planes;
    (void)img; (void)img_length;

    return 0;
}

#endif /* HAVE_PNG */
