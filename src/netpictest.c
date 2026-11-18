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
 * The pictures the gateway makes for EWeb, from pictures made here: what is
 * made over and what goes as it came, the size a smaller one says it had -
 * read back the way EWeb's img.c reads it - and pens in the form EWeb
 * unpacks. What a build without one of the libraries cannot do is said and
 * not checked.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netpic.h"

#ifdef HAVE_JPEG
#include <jpeglib.h>
#endif
#ifdef HAVE_WEBP
#include <webp/encode.h>
#endif

static int n;
static int fails;

static void check(long got, long want, const char *name)
{
    n++;

    if (got == want)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s (got %ld, want %ld)\n", n, name, got, want);
    }
}

static void check_str(const char *got, const char *want, const char *name)
{
    n++;

    if (got && strcmp(got, want) == 0)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s\n# got:  [%s]\n# want: [%s]\n", n, name, got ? got : "(none)", want);
    }
}

/* A picture: a gradient, or one colour, with the left half see-through when
 * asked */
static struct netpic make(int w, int h, int solid, uint32_t rgb, int clear_left)
{
    struct netpic p;
    int x, y;

    p.w = w;
    p.h = h;
    p.rgba = malloc((size_t)w * h * 4);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            uint8_t *o = p.rgba + ((size_t)y * w + x) * 4;

            if (solid)
            {
                o[0] = (uint8_t)(rgb >> 16);
                o[1] = (uint8_t)(rgb >> 8);
                o[2] = (uint8_t)rgb;
            }
            else
            {
                o[0] = (uint8_t)(x * 255 / (w - 1));
                o[1] = (uint8_t)(y * 255 / (h - 1));
                o[2] = (uint8_t)((x + y) & 255);
            }
            o[3] = clear_left && x < w / 2 ? 0 : 255;
        }

    return p;
}

/* The size a smaller picture says it had, read as EWeb's img.c reads it */
static int natural(const uint8_t *d, size_t len, int *w, int *h)
{
    const uint8_t *t = NULL;
    size_t i, tl = 0;
    char s[32];

    if (netpic_kind(d, len) == NETPIC_JPEG)
    {
        for (i = 2; i + 4 <= len && d[i] == 0xff;)
        {
            size_t sl = (size_t)d[i + 2] << 8 | d[i + 3];

            if (d[i + 1] == 0xda || sl < 2)
                break;
            if (d[i + 1] == 0xfe && sl - 2 > 16 && !memcmp(d + i + 4, "webgate natural ", 16))
            {
                t = d + i + 4 + 16;
                tl = sl - 2 - 16;
                break;
            }
            i += 2 + sl;
        }
    }
    else
    {
        for (i = 8; i + 12 <= len;)
        {
            size_t cl = (size_t)d[i] << 24 | (size_t)d[i + 1] << 16 | (size_t)d[i + 2] << 8 | d[i + 3];

            if (!memcmp(d + i + 4, "IDAT", 4))
                break;
            if (!memcmp(d + i + 4, "tEXt", 4) && cl > 16 && !memcmp(d + i + 8, "webgate\0natural ", 16))
            {
                t = d + i + 8 + 16;
                tl = cl - 16;
                break;
            }
            i += 12 + cl;
        }
    }

    if (!t || tl >= sizeof s)
        return 0;
    memcpy(s, t, tl);
    s[tl] = 0;
    return sscanf(s, "%dx%d", w, h) == 2;
}

static void check_asking(void)
{
    struct netpic_ask a;

    check(netpic_crc32((const uint8_t *)"123456789", 9), 0xcbf43926L, "CRC-32 is the usual one");

    check(netpic_parse_ask("fit=608; size=400x300; pens=16; pal=1a2b3c4d; ed=tj", &a), 0,
          "a request for pens is read");
    check(a.fit == 608 && a.w == 400 && a.h == 300 && a.pens == 16 && a.pal == 0x1a2b3c4d, 1,
          "with all of it");
    check(netpic_parse_ask("fit=608; deep=1", &a) == 0 && a.deep && !a.pens, 1,
          "and one for a true colour screen");
    check(netpic_parse_ask("pens=16", &a), -1, "one without a width is not one");
    check(netpic_parse_ask("fit=608;pens=1", &a), -1, "nor is one pen");
}

static void check_kinds(void)
{
    check(netpic_kind((const uint8_t *)"\xff\xd8\xff\xe0", 4), NETPIC_JPEG, "a JPEG is known");
    check(netpic_kind((const uint8_t *)"\x89PNG\r\n\x1a\n", 8), NETPIC_PNG, "and a PNG");
    check(netpic_kind((const uint8_t *)"GIF89a", 6), NETPIC_GIF, "and a GIF");
    check(netpic_kind((const uint8_t *)"RIFF\0\0\0\0WEBP", 12), NETPIC_WEBP, "and a WebP");
    check(netpic_kind((const uint8_t *)"<html>", 6), NETPIC_OTHER, "and a page is none of them");
}

static void check_scaling(void)
{
    struct netpic in = make(4, 1, 1, 0, 0), out;

    in.rgba[0] = in.rgba[1] = in.rgba[2] = 0;
    in.rgba[4] = in.rgba[5] = in.rgba[6] = 200;
    check(netpic_scale(&in, 2, 1, &out), 0, "a picture is made half as wide");
    check(out.rgba[0], 100, "each pixel the average of the two it covers");
    netpic_free(&out);
    netpic_free(&in);

    in = make(4, 1, 1, 0xff0000, 1);
    netpic_scale(&in, 2, 1, &out);
    check(out.rgba[3] == 0 && out.rgba[7] == 255, 1, "and what was see-through stays so");
    netpic_free(&out);
    netpic_free(&in);
}

#ifdef HAVE_JPEG
/* A progressive JPEG, which netpic_jpeg never writes */
static uint8_t *progressive_jpeg(const struct netpic *pic, size_t *len)
{
    struct jpeg_compress_struct c;
    struct jpeg_error_mgr e;
    unsigned char *buf = NULL;
    unsigned long size = 0;
    uint8_t *row = malloc((size_t)pic->w * 3), *out;
    int x;

    c.err = jpeg_std_error(&e);
    jpeg_create_compress(&c);
    jpeg_mem_dest(&c, &buf, &size);
    c.image_width = pic->w;
    c.image_height = pic->h;
    c.input_components = 3;
    c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_simple_progression(&c);
    jpeg_start_compress(&c, TRUE);
    while (c.next_scanline < c.image_height)
    {
        JSAMPROW r = row;

        for (x = 0; x < pic->w; x++)
            memcpy(row + x * 3, pic->rgba + ((size_t)c.next_scanline * pic->w + x) * 4, 3);
        jpeg_write_scanlines(&c, &r, 1);
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    free(row);

    out = malloc(size);
    memcpy(out, buf, size);
    free(buf);
    *len = size;
    return out;
}

/* The same picture with EXIF saying it was taken turned a quarter, which is
 * an APP1 put in after the start */
static uint8_t *turned(const uint8_t *jpeg, size_t len, size_t *outlen)
{
    static const uint8_t app1[] = {
        0xff, 0xe1, 0x00, 0x22,
        'E', 'x', 'i', 'f', 0, 0,
        'M', 'M', 0x00, 0x2a, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x01,
        0x01, 0x12, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x06, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    uint8_t *out = malloc(len + sizeof app1);

    memcpy(out, jpeg, 2);
    memcpy(out + 2, app1, sizeof app1);
    memcpy(out + 2 + sizeof app1, jpeg + 2, len - 2);
    *outlen = len + sizeof app1;
    return out;
}

static void check_jpeg(void)
{
    struct netpic pic = make(800, 400, 0, 0, 0), back;
    struct netpic_ask ask;
    uint8_t *jpeg, *made, *prog, *side;
    size_t len, made_len, prog_len, side_len;
    const char *type;
    char what[96];
    int w, h;

    jpeg = netpic_jpeg(&pic, 80, pic.w, pic.h, &len);
    check(jpeg != NULL && netpic_kind(jpeg, len) == NETPIC_JPEG, 1, "a JPEG is written");
    check(netpic_progressive(jpeg, len), 0, "a baseline one");
    check(natural(jpeg, len, &w, &h), 0, "saying nothing of a size it always had");

    netpic_parse_ask("fit=400", &ask);
    check(netpic_convert(&ask, jpeg, len, &made, &made_len, &type, what, sizeof what),
          NETPIC_MADE, "one wider than the page is made over");
    check_str(type, "image/jpeg", "as a JPEG");
    check_str(what, "JPEG 800x400 to JPEG 400x200", "saying so");
    check(netpic_decode(made, made_len, 0, &back) == 0 && back.w == 400 && back.h == 200, 1,
          "as wide as the page");
    check(natural(made, made_len, &w, &h) && w == 800 && h == 400, 1,
          "saying in its comment how large it was");
    netpic_free(&back);
    free(made);

    netpic_parse_ask("fit=1024", &ask);
    check(netpic_convert(&ask, jpeg, len, &made, &made_len, &type, what, sizeof what),
          NETPIC_ASIS, "one that fits goes as it came");

    prog = progressive_jpeg(&pic, &prog_len);
    check(netpic_progressive(prog, prog_len), 1, "a progressive JPEG is seen to be one");
    check(netpic_convert(&ask, prog, prog_len, &made, &made_len, &type, what, sizeof what),
          NETPIC_MADE, "and is made over though it fits, EWeb showing only its averages");
    check(netpic_progressive(made, made_len), 0, "into a baseline one");
    check(natural(made, made_len, &w, &h), 0, "of the same size, so saying none");
    free(made);
    free(prog);

    side = turned(jpeg, len, &side_len);
    check(netpic_decode(side, side_len, 0, &back) == 0 && back.w == 400 && back.h == 800, 1,
          "a JPEG taken on its side is read upright");
    if (back.rgba)
        check(back.rgba[0] < 40 && back.rgba[1] > 200, 1,
              "turned the right way: the bottom left corner comes to the top left");
    netpic_free(&back);
    free(side);

    free(jpeg);
    netpic_free(&pic);
}
#endif

#ifdef HAVE_PNG
static void check_png(void)
{
    struct netpic pic = make(300, 100, 1, 0x3366cc, 1), back;
    struct netpic_ask ask;
    uint8_t *png, *made;
    size_t len, made_len;
    const char *type;
    char what[96];
    int w, h;

    png = netpic_png(&pic, pic.w, pic.h, &len);
    check(png != NULL && netpic_kind(png, len) == NETPIC_PNG, 1, "a PNG is written");
    check(png ? png[25] : -1, 3, "of the picture's own colours where there are few");

    netpic_parse_ask("fit=150", &ask);
    check(netpic_convert(&ask, png, len, &made, &made_len, &type, what, sizeof what),
          NETPIC_MADE, "one wider than the page is made over");
    check_str(type, "image/png", "as a PNG, since it is see-through");
    check(natural(made, made_len, &w, &h) && w == 300 && h == 100, 1,
          "saying in its text how large it was");
    check(netpic_decode(made, made_len, 0, &back) == 0 && back.w == 150 && back.h == 50, 1,
          "as wide as the page");
    if (back.rgba)
        check(back.rgba[3] == 0 && back.rgba[(149) * 4 + 3] == 255, 1,
              "and still see-through on the left");
    netpic_free(&back);
    free(made);
    free(png);
    netpic_free(&pic);
}

static void check_pens(void)
{
    static const uint8_t rgb[] = { 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0, 255 };
    struct netpic red = make(16, 8, 1, 0xff0000, 0), half = make(16, 8, 1, 0x0000ff, 1);
    struct netpic_ask ask;
    uint8_t *png, *made;
    size_t len, made_len, i;
    const char *type;
    char what[96], asked[96];
    uint32_t crc;
    int all_red = 1;

    netpic_parse_ask("fit=608; size=16x8; pens=4; pal=00000001", &ask);
    png = netpic_png(&red, red.w, red.h, &len);
    check(netpic_convert(&ask, png, len, &made, &made_len, &type, what, sizeof what),
          NETPIC_NEED_PAL, "pens in colours never sent are a 409");

    check(netpic_palette_add(rgb, 3), 0, "one colour is not a palette");
    crc = netpic_palette_add(rgb, sizeof rgb);
    check(crc, (long)netpic_crc32(rgb, sizeof rgb), "four are, known by their CRC");
    check(netpic_palette_known(crc), 1, "and kept");

    snprintf(asked, sizeof asked, "fit=608; size=16x8; pens=4; pal=%08x; ed=tj", crc);
    netpic_parse_ask(asked, &ask);
    check(netpic_convert(&ask, png, len, &made, &made_len, &type, what, sizeof what),
          NETPIC_MADE, "pens are made");
    check_str(type, "application/x-aera-pens", "as pens");
    check(made_len, 20 + 8 * 4, "of the length EWeb checks them against");
    check(made && memcmp(made, "EWPN\1", 5) == 0 && made[5] == 2 && made[6] == 0 && made[7] == 3, 1,
          "two bits a pen for four pens, and no mask");
    check(made && made[9] == 16 && made[11] == 8 && made[13] == 16 && made[15] == 8, 1,
          "the size made and the size it had");
    check(made && (uint32_t)(made[16] << 24 | made[17] << 16 | made[18] << 8 | made[19]) == crc, 1,
          "and whose colours");
    for (i = 20; made && i < made_len; i++)
        if (made[i] != 0xaa)
            all_red = 0;
    check(all_red, 1, "a picture of one of the colours is that pen throughout");
    free(made);
    free(png);

    png = netpic_png(&half, half.w, half.h, &len);
    netpic_convert(&ask, png, len, &made, &made_len, &type, what, sizeof what);
    check(made && made[6] == 1 && made_len == 20 + 8 * 4 + 8 * 2, 1,
          "a see-through one has a mask after its pens");
    check(made && made[20 + 8 * 4] == 0x00 && made[20 + 8 * 4 + 1] == 0xff, 1,
          "set where it is shown");
    check(made && made[20] == 0x00 && made[22] == 0xff, 1,
          "and the shown part blue, the fourth pen");
    free(made);
    free(png);

    netpic_free(&red);
    netpic_free(&half);
}
#endif

#if defined(HAVE_PNG) && defined(HAVE_JPEG)
/* Noise, which nothing writes small: the largest picture there is of a size */
static struct netpic noise(int w, int h)
{
    struct netpic p;
    unsigned long seed = 12345;
    size_t i;

    p.w = w;
    p.h = h;
    p.rgba = malloc((size_t)w * h * 4);
    for (i = 0; i < (size_t)w * h * 4; i++)
    {
        seed = seed * 1103515245ul + 12345ul;
        p.rgba[i] = (i & 3) == 3 ? 255 : (uint8_t)(seed >> 16);
    }
    return p;
}

/*
 * Nothing larger than EWeb takes. The colours are a palette of their own,
 * kept apart from check_pens's.
 */
static void check_budget(void)
{
    static uint8_t rgb[16 * 3];
    struct netpic big = noise(1024, 768), small = noise(800, 600);
    struct netpic_ask ask;
    uint8_t *jpeg, *made;
    size_t len, made_len;
    const char *type;
    char what[128], asked[96];
    uint32_t crc;
    int i, w, h;

    jpeg = netpic_jpeg(&big, 95, big.w, big.h, &len);
    check(len > NETPIC_BUDGET, 1, "a JPEG of noise is larger than EWeb takes");

    netpic_parse_ask("fit=1390", &ask);
    check(netpic_convert(&ask, jpeg, len, &made, &made_len, &type, what, sizeof what),
          NETPIC_MADE, "so it is made over though it fits");
    check(made_len <= NETPIC_BUDGET, 1, "into one EWeb takes");
    check(strstr(what, "quality") != NULL, 1, "saying how far it was let down");
    if (made && strstr(what, "to JPEG 1024x768") == NULL)
        check(natural(made, made_len, &w, &h) && w == 1024 && h == 768, 1,
              "and, made smaller as well, saying how large it was");
    free(made);
    free(jpeg);

    for (i = 0; i < 16; i++)
    {
        rgb[i * 3] = (uint8_t)(i * 17);
        rgb[i * 3 + 1] = (uint8_t)(255 - i * 17);
        rgb[i * 3 + 2] = (uint8_t)(i * 8);
    }
    crc = netpic_palette_add(rgb, sizeof rgb);
    jpeg = netpic_jpeg(&small, 90, small.w, small.h, &len);
    snprintf(asked, sizeof asked, "fit=1390; size=800x600; pens=16; pal=%08x; ed=tj", crc);
    netpic_parse_ask(asked, &ask);
    check(netpic_convert(&ask, jpeg, len, &made, &made_len, &type, what, sizeof what),
          NETPIC_MADE, "pens larger than EWeb takes");
    check_str(type, "image/jpeg", "come as a JPEG instead");
    check(made_len <= NETPIC_BUDGET, 1, "that it does take");
    check(strstr(what, "not pens") != NULL, 1, "saying why");
    free(made);

    /* A machine with more room free, which is the same pens taken whole */
    ask.budget = 512L * 1024;
    check(netpic_convert(&ask, jpeg, len, &made, &made_len, &type, what, sizeof what),
          NETPIC_MADE, "on a machine with more memory free");
    check_str(type, "application/x-aera-pens", "the same pens come as pens");
    free(made);

    check(netpic_budget(4968L * 1024) < 4968L * 1024 / 32, 1,
          "the budget is under what EWeb takes of the memory free");
    check(netpic_budget(4968L * 1024) > 100L * 1024, 1, "but not far under it");
    check(netpic_budget(0), 32L * 1024, "and never less than a small picture");
    free(jpeg);

    netpic_free(&big);
    netpic_free(&small);
}
#endif

#if defined(HAVE_WEBP) && defined(HAVE_JPEG)
static void check_webp(void)
{
    struct netpic pic = make(200, 100, 0, 0, 0);
    struct netpic_ask ask;
    uint8_t *webp, *made;
    size_t len, made_len;
    const char *type;
    char what[96];
    int w, h;

    len = WebPEncodeRGBA(pic.rgba, pic.w, pic.h, pic.w * 4, 80, &webp);
    check(len > 0 && netpic_kind(webp, len) == NETPIC_WEBP, 1, "a WebP is made to read");

    netpic_parse_ask("fit=608", &ask);
    check(netpic_convert(&ask, webp, len, &made, &made_len, &type, what, sizeof what),
          NETPIC_MADE, "a WebP is made over though it fits, the ST reading none");
    check_str(what, "WEBP 200x100 to JPEG 200x100", "into a JPEG of the same size");
    check(natural(made, made_len, &w, &h), 0, "which says none");
    free(made);
    WebPFree(webp);
    netpic_free(&pic);
}
#endif

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    check_asking();
    check_kinds();
    check_scaling();
#ifdef HAVE_JPEG
    check_jpeg();
#else
    printf("# built without libjpeg, so no JPEG is checked\n");
#endif
#ifdef HAVE_PNG
    check_png();
    check_pens();
#else
    printf("# built without libpng, so no PNG and no pens are checked\n");
#endif
#if defined(HAVE_PNG) && defined(HAVE_JPEG)
    check_budget();
#endif
#if defined(HAVE_WEBP) && defined(HAVE_JPEG)
    check_webp();
#else
    printf("# built without libwebp, so no WebP is checked\n");
#endif

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
