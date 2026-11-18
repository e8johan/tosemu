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
 * Pictures made over for EWeb - see netpic.h for what is asked and why.
 *
 * Read with libjpeg, libpng and libwebp, each where the build found it; a
 * picture of a kind nothing here reads goes on as it came, and EWeb does
 * what it can with it. A GIF always does: EWeb reads those itself.
 */

#include "netpic.h"

#include <ctype.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_JPEG
#include <jpeglib.h>
#endif
#ifdef HAVE_PNG
#include <png.h>
#endif
#ifdef HAVE_WEBP
#include <webp/decode.h>
#include <webp/demux.h>
#endif

/* The most a picture may have, as webgate allows */
#define MOST_PIXELS (40L * 1000 * 1000)


/* Asking ********************************************************************/

int netpic_parse_ask(const char *v, struct netpic_ask *a)
{
    memset(a, 0, sizeof *a);

    while (*v)
    {
        char key[16], val[32];
        size_t k = 0, n = 0;

        while (*v == ' ' || *v == '\t' || *v == ';')
            v++;
        if (!*v)
            break;

        while (*v && *v != '=' && *v != ';' && k + 1 < sizeof key)
            key[k++] = (char)tolower((unsigned char)*v++);
        key[k] = 0;
        if (*v == '=')
            v++;
        while (*v && *v != ';' && *v != ' ' && n + 1 < sizeof val)
            val[n++] = *v++;
        val[n] = 0;
        while (*v && *v != ';')
            v++;

        if (strcmp(key, "fit") == 0)
            a->fit = strtol(val, NULL, 10);
        else if (strcmp(key, "deep") == 0)
            a->deep = strcmp(val, "1") == 0;
        else if (strcmp(key, "pens") == 0)
            a->pens = (int)strtol(val, NULL, 10);
        else if (strcmp(key, "pal") == 0)
            a->pal = (uint32_t)strtoul(val, NULL, 16);
        else if (strcmp(key, "size") == 0)
        {
            char *x;

            a->w = (int)strtol(val, &x, 10);
            a->h = *x == 'x' ? (int)strtol(x + 1, NULL, 10) : 0;
            if (a->w < 1 || a->h < 1 || a->w > 4096 || a->h > 4096)
                a->w = a->h = 0;
        }
    }

    if (a->fit < 16 || a->fit > 4096)
        return -1;
    if (a->pens && (a->pens < 2 || a->pens > 256))
        return -1;

    return 0;
}

int netpic_kind(const uint8_t *d, size_t n)
{
    if (n >= 3 && d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff)
        return NETPIC_JPEG;
    if (n >= 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0)
        return NETPIC_PNG;
    if (n >= 6 && (memcmp(d, "GIF87a", 6) == 0 || memcmp(d, "GIF89a", 6) == 0))
        return NETPIC_GIF;
    if (n >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0)
        return NETPIC_WEBP;
    return NETPIC_OTHER;
}

static const char *kind_name(int kind)
{
    switch (kind)
    {
    case NETPIC_JPEG: return "JPEG";
    case NETPIC_PNG:  return "PNG";
    case NETPIC_GIF:  return "GIF";
    case NETPIC_WEBP: return "WEBP";
    default:          return "picture";
    }
}

/* The markers ahead of the scan, which say how the picture was coded */
int netpic_progressive(const uint8_t *d, size_t n)
{
    size_t i = 2;

    while (i + 4 <= n && d[i] == 0xff)
    {
        uint8_t m = d[i + 1];
        size_t len = (size_t)d[i + 2] << 8 | d[i + 3];

        if (m == 0xda)
            break;
        if (m == 0xc2 || m == 0xc6 || m == 0xca || m == 0xce)
            return 1;
        if (len < 2)
            break;
        i += 2 + len;
    }

    return 0;
}

uint32_t netpic_crc32(const uint8_t *d, size_t n)
{
    uint32_t c = 0xffffffffu;
    size_t i;
    int k;

    for (i = 0; i < n; i++)
    {
        c ^= d[i];
        for (k = 0; k < 8; k++)
            c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1;
    }

    return c ^ 0xffffffffu;
}

/* The palettes ***************************************************************/

/* A few screens' worth: a palette is a screen's, and there is one Atari */
#define PALETTES (16)

static struct
{
    uint32_t crc;
    int n;
    uint8_t rgb[256 * 3];
    unsigned long used;
} palettes[PALETTES];

static unsigned long palette_clock;
static pthread_mutex_t palette_lock = PTHREAD_MUTEX_INITIALIZER;

uint32_t netpic_palette_add(const uint8_t *rgb, size_t len)
{
    uint32_t crc;
    int i, oldest = 0;

    if (len % 3 != 0 || len < 2 * 3 || len > 256 * 3)
        return 0;

    crc = netpic_crc32(rgb, len);

    pthread_mutex_lock(&palette_lock);
    for (i = 0; i < PALETTES; i++)
    {
        if (palettes[i].n && palettes[i].crc == crc)
            break;
        if (palettes[i].used < palettes[oldest].used)
            oldest = i;
    }
    if (i == PALETTES)
    {
        i = oldest;
        palettes[i].crc = crc;
        palettes[i].n = (int)(len / 3);
        memcpy(palettes[i].rgb, rgb, len);
    }
    palettes[i].used = ++palette_clock;
    pthread_mutex_unlock(&palette_lock);

    return crc;
}

/* A copy of a palette, so that it can be used without the lock. Its size,
 * or 0 for one not known. */
static int palette_get(uint32_t crc, uint8_t *rgb)
{
    int i, n = 0;

    pthread_mutex_lock(&palette_lock);
    for (i = 0; i < PALETTES; i++)
        if (palettes[i].n && palettes[i].crc == crc)
        {
            n = palettes[i].n;
            memcpy(rgb, palettes[i].rgb, n * 3);
            palettes[i].used = ++palette_clock;
            break;
        }
    pthread_mutex_unlock(&palette_lock);

    return n;
}

int netpic_palette_known(uint32_t crc)
{
    uint8_t rgb[256 * 3];

    return palette_get(crc, rgb) != 0;
}

/* Reading ********************************************************************/

void netpic_free(struct netpic *pic)
{
    free(pic->rgba);
    pic->rgba = NULL;
    pic->w = pic->h = 0;
}

static int alloc_pic(struct netpic *pic, int w, int h)
{
    if (w < 1 || h < 1 || (long)w * h > MOST_PIXELS)
        return -1;

    pic->w = w;
    pic->h = h;
    pic->rgba = malloc((size_t)w * h * 4);

    return pic->rgba ? 0 : -1;
}

#ifdef HAVE_JPEG
/*
 * EXIF's orientation, 1 to 8: how the stored picture is to be turned to
 * stand upright. 1 is as stored.
 */
static int exif_orientation(const uint8_t *d, size_t n)
{
    size_t ifd, i, entries;
    int le;

#define U16(p) (le ? (d[p] | d[(p) + 1] << 8) : (d[p] << 8 | d[(p) + 1]))
#define U32(p) (le ? ((uint32_t)d[p] | (uint32_t)d[(p) + 1] << 8 | (uint32_t)d[(p) + 2] << 16 | (uint32_t)d[(p) + 3] << 24) \
                   : ((uint32_t)d[p] << 24 | (uint32_t)d[(p) + 1] << 16 | (uint32_t)d[(p) + 2] << 8 | (uint32_t)d[(p) + 3]))

    if (n < 14 || memcmp(d, "Exif\0\0", 6) != 0)
        return 1;
    d += 6;
    n -= 6;

    if (memcmp(d, "II", 2) == 0)
        le = 1;
    else if (memcmp(d, "MM", 2) == 0)
        le = 0;
    else
        return 1;

    ifd = U32(4);
    if (ifd + 2 > n)
        return 1;
    entries = U16(ifd);

    for (i = 0; i < entries; i++)
    {
        size_t e = ifd + 2 + i * 12;

        if (e + 12 > n)
            break;
        if (U16(e) == 0x0112)
        {
            int o = U16(e + 8);

            return o >= 1 && o <= 8 ? o : 1;
        }
    }

    return 1;
#undef U16
#undef U32
}

/* The picture turned as EXIF said */
static int orient(struct netpic *pic, int o)
{
    struct netpic out;
    int x, y, w = pic->w, h = pic->h;
    int swap = o >= 5;

    if (o <= 1)
        return 0;
    if (alloc_pic(&out, swap ? h : w, swap ? w : h) < 0)
        return -1;

    for (y = 0; y < out.h; y++)
        for (x = 0; x < out.w; x++)
        {
            int sx, sy;

            switch (o)
            {
            case 2: sx = w - 1 - x; sy = y; break;
            case 3: sx = w - 1 - x; sy = h - 1 - y; break;
            case 4: sx = x; sy = h - 1 - y; break;
            case 5: sx = y; sy = x; break;
            case 6: sx = y; sy = h - 1 - x; break;
            case 7: sx = w - 1 - y; sy = h - 1 - x; break;
            default: sx = w - 1 - y; sy = x; break;
            }
            memcpy(out.rgba + ((size_t)y * out.w + x) * 4,
                   pic->rgba + ((size_t)sy * w + sx) * 4, 4);
        }

    netpic_free(pic);
    *pic = out;
    return 0;
}

struct jpeg_trap
{
    struct jpeg_error_mgr mgr;
    jmp_buf out;
};

static void jpeg_trap_exit(j_common_ptr c)
{
    longjmp(((struct jpeg_trap *)c->err)->out, 1);
}

static void jpeg_quiet(j_common_ptr c)
{
    (void)c;
}

/*
 * Read at an eighth, a quarter or a half where the picture is to be that
 * much smaller anyway, which libjpeg does for little more than the cost of
 * reading the file. own_w, own_h are the picture's own size, upright.
 */
static int decode_jpeg(const uint8_t *d, size_t n, long want_w, struct netpic *pic,
                       int *own_w, int *own_h)
{
    struct jpeg_decompress_struct c;
    struct jpeg_trap trap;
    jpeg_saved_marker_ptr m;
    uint8_t *row = NULL;
    volatile int o = 1;
    int cmyk, y;

    memset(pic, 0, sizeof *pic);

    c.err = jpeg_std_error(&trap.mgr);
    trap.mgr.error_exit = jpeg_trap_exit;
    trap.mgr.output_message = jpeg_quiet;
    if (setjmp(trap.out))
    {
        jpeg_destroy_decompress(&c);
        free(row);
        netpic_free(pic);
        return -1;
    }

    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, (unsigned char *)d, (unsigned long)n);
    jpeg_save_markers(&c, JPEG_APP0 + 1, 0xffff);
    jpeg_read_header(&c, TRUE);

    for (m = c.marker_list; m; m = m->next)
        if (m->marker == JPEG_APP0 + 1)
            o = exif_orientation(m->data, m->data_length);

    *own_w = (int)(o >= 5 ? c.image_height : c.image_width);
    *own_h = (int)(o >= 5 ? c.image_width : c.image_height);

    if ((long)c.image_width * c.image_height > MOST_PIXELS)
        longjmp(trap.out, 1);

    if (want_w > 0)
    {
        long across = *own_w;
        int denom = 8;

        while (denom > 1 && (across + denom - 1) / denom < want_w)
            denom /= 2;
        c.scale_num = 1;
        c.scale_denom = denom;
    }

    cmyk = c.jpeg_color_space == JCS_CMYK || c.jpeg_color_space == JCS_YCCK;
    c.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;

    jpeg_start_decompress(&c);

    if (alloc_pic(pic, (int)c.output_width, (int)c.output_height) < 0
        || !(row = malloc((size_t)c.output_width * c.output_components)))
        longjmp(trap.out, 1);

    for (y = 0; c.output_scanline < c.output_height; y++)
    {
        uint8_t *o8 = pic->rgba + (size_t)y * pic->w * 4;
        JSAMPROW r = row;
        int x;

        jpeg_read_scanlines(&c, &r, 1);
        for (x = 0; x < pic->w; x++)
        {
            if (cmyk)
            {
                /* Adobe's, which is stored inverted */
                int k = row[x * 4 + 3];

                o8[x * 4 + 0] = (uint8_t)(row[x * 4 + 0] * k / 255);
                o8[x * 4 + 1] = (uint8_t)(row[x * 4 + 1] * k / 255);
                o8[x * 4 + 2] = (uint8_t)(row[x * 4 + 2] * k / 255);
            }
            else
                memcpy(o8 + x * 4, row + x * 3, 3);
            o8[x * 4 + 3] = 255;
        }
    }

    jpeg_finish_decompress(&c);
    jpeg_destroy_decompress(&c);
    free(row);

    return orient(pic, o);
}
#endif

#ifdef HAVE_PNG
static int decode_png(const uint8_t *d, size_t n, struct netpic *pic)
{
    png_image img;

    memset(pic, 0, sizeof *pic);
    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&img, d, n))
        return -1;

    img.format = PNG_FORMAT_RGBA;
    if (alloc_pic(pic, (int)img.width, (int)img.height) < 0)
    {
        png_image_free(&img);
        return -1;
    }

    if (!png_image_finish_read(&img, NULL, pic->rgba, 0, NULL))
    {
        png_image_free(&img);
        netpic_free(pic);
        return -1;
    }

    return 0;
}
#endif

#ifdef HAVE_WEBP
/* The first frame of one that moves, on its canvas */
static int decode_webp(const uint8_t *d, size_t n, struct netpic *pic)
{
    WebPData data = { d, n };
    WebPDemuxer *demux;
    WebPIterator it;
    uint8_t *rgba;
    int w, h, y;

    memset(pic, 0, sizeof *pic);

    rgba = WebPDecodeRGBA(d, n, &w, &h);
    if (rgba)
    {
        if (alloc_pic(pic, w, h) < 0)
        {
            WebPFree(rgba);
            return -1;
        }
        memcpy(pic->rgba, rgba, (size_t)w * h * 4);
        WebPFree(rgba);
        return 0;
    }

    demux = WebPDemux(&data);
    if (!demux)
        return -1;

    w = (int)WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH);
    h = (int)WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT);
    if (!WebPDemuxGetFrame(demux, 1, &it) || alloc_pic(pic, w, h) < 0)
    {
        WebPDemuxDelete(demux);
        return -1;
    }

    memset(pic->rgba, 0, (size_t)w * h * 4);
    rgba = WebPDecodeRGBA(it.fragment.bytes, it.fragment.size, &w, &h);
    if (rgba)
    {
        for (y = 0; y < h && it.y_offset + y < pic->h; y++)
        {
            int across = it.x_offset + w > pic->w ? pic->w - it.x_offset : w;

            if (across > 0)
                memcpy(pic->rgba + ((size_t)(it.y_offset + y) * pic->w + it.x_offset) * 4,
                       rgba + (size_t)y * w * 4, (size_t)across * 4);
        }
        WebPFree(rgba);
    }

    WebPDemuxReleaseIterator(&it);
    WebPDemuxDelete(demux);

    if (!rgba)
    {
        netpic_free(pic);
        return -1;
    }
    return 0;
}
#endif

/*
 * Reads a picture, upright. want_w is how wide it is going to be made, which
 * lets a JPEG be read at a fraction of its size; the picture comes out at
 * least that wide where it was. Its own size, which it may be read smaller
 * than, is not kept here - the caller has it from netpic_convert.
 */
static int decode(const uint8_t *d, size_t n, long want_w, struct netpic *pic,
                  int *own_w, int *own_h)
{
    int r = -1;

    memset(pic, 0, sizeof *pic);

    switch (netpic_kind(d, n))
    {
#ifdef HAVE_JPEG
    case NETPIC_JPEG:
        return decode_jpeg(d, n, want_w, pic, own_w, own_h);
#endif
#ifdef HAVE_PNG
    case NETPIC_PNG:
        r = decode_png(d, n, pic);
        break;
#endif
#ifdef HAVE_WEBP
    case NETPIC_WEBP:
        r = decode_webp(d, n, pic);
        break;
#endif
    }

    if (r == 0)
    {
        *own_w = pic->w;
        *own_h = pic->h;
    }
    return r;
}

int netpic_decode(const uint8_t *d, size_t n, long want_w, struct netpic *pic)
{
    int w, h;

    return decode(d, n, want_w, pic, &w, &h);
}

/* Making smaller ************************************************************/

/*
 * Each output pixel along one axis, as the source pixels under it and how
 * much of each. Made smaller, a pixel is the average of the area it covers;
 * made larger, it is between the two nearest.
 */
struct span
{
    int first, count;
    float *weight;
};

static struct span *spans(int from, int to)
{
    struct span *s = calloc(to, sizeof *s);
    double scale = (double)from / to;
    int i, j;

    if (!s)
        return NULL;

    for (i = 0; i < to; i++)
    {
        if (scale > 1)
        {
            double a = i * scale, b = (i + 1) * scale;
            int lo = (int)a, hi = (int)b;

            if (hi >= from)
                hi = from - 1;
            s[i].first = lo;
            s[i].count = hi - lo + 1;
            s[i].weight = malloc(s[i].count * sizeof(float));
            if (!s[i].weight)
                return s;
            for (j = 0; j < s[i].count; j++)
            {
                double l = lo + j, r = l + 1;

                if (l < a)
                    l = a;
                if (r > b)
                    r = b;
                s[i].weight[j] = (float)((r > l ? r - l : 0) / scale);
            }
        }
        else
        {
            double c = (i + 0.5) * scale - 0.5;
            int lo = c < 0 ? 0 : (int)c;
            double f = c - lo;

            if (c < 0)
                f = 0;
            if (lo >= from - 1)
            {
                lo = from - 1;
                f = 0;
            }
            s[i].first = lo;
            s[i].count = f > 0 ? 2 : 1;
            s[i].weight = malloc(2 * sizeof(float));
            if (!s[i].weight)
                return s;
            s[i].weight[0] = (float)(1 - f);
            s[i].weight[1] = (float)f;
        }
    }

    return s;
}

static void spans_free(struct span *s, int n)
{
    int i;

    if (!s)
        return;
    for (i = 0; i < n; i++)
        free(s[i].weight);
    free(s);
}

/* Across, then down, in colours multiplied by their alpha so that what is
 * transparent does not bleed its colour into what is not */
int netpic_scale(const struct netpic *in, int w, int h, struct netpic *out)
{
    struct span *sx = spans(in->w, w), *sy = spans(in->h, h);
    float *mid = NULL;
    int x, y, j, r = -1;

    memset(out, 0, sizeof *out);

    if (!sx || !sy || alloc_pic(out, w, h) < 0
        || !(mid = malloc((size_t)w * in->h * 4 * sizeof(float))))
        goto done;
    for (x = 0; x < w; x++)
        if (!sx[x].weight)
            goto done;
    for (y = 0; y < h; y++)
        if (!sy[y].weight)
            goto done;

    for (y = 0; y < in->h; y++)
    {
        const uint8_t *row = in->rgba + (size_t)y * in->w * 4;

        for (x = 0; x < w; x++)
        {
            float acc[4] = { 0, 0, 0, 0 };

            for (j = 0; j < sx[x].count; j++)
            {
                const uint8_t *p = row + (size_t)(sx[x].first + j) * 4;
                float wt = sx[x].weight[j] * p[3] / 255.0f;

                acc[0] += p[0] * wt;
                acc[1] += p[1] * wt;
                acc[2] += p[2] * wt;
                acc[3] += p[3] * sx[x].weight[j];
            }
            memcpy(mid + ((size_t)y * w + x) * 4, acc, sizeof acc);
        }
    }

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            float acc[4] = { 0, 0, 0, 0 };
            uint8_t *o = out->rgba + ((size_t)y * w + x) * 4;
            int k;

            for (j = 0; j < sy[y].count; j++)
            {
                const float *p = mid + ((size_t)(sy[y].first + j) * w + x) * 4;

                for (k = 0; k < 4; k++)
                    acc[k] += p[k] * sy[y].weight[j];
            }

            for (k = 0; k < 3; k++)
            {
                float v = acc[3] > 0.01f ? acc[k] * 255.0f / acc[3] : 0;

                o[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
            }
            o[3] = (uint8_t)(acc[3] < 0 ? 0 : acc[3] > 255 ? 255 : acc[3] + 0.5f);
        }

    r = 0;

done:
    free(mid);
    spans_free(sx, w);
    spans_free(sy, h);
    if (r < 0)
        netpic_free(out);
    return r;
}

/* Writing *******************************************************************/

static int has_alpha(const struct netpic *pic)
{
    size_t i, n = (size_t)pic->w * pic->h;

    for (i = 0; i < n; i++)
        if (pic->rgba[i * 4 + 3] != 255)
            return 1;
    return 0;
}

#ifdef HAVE_JPEG
uint8_t *netpic_jpeg(const struct netpic *pic, int quality, int nat_w, int nat_h, size_t *len)
{
    struct jpeg_compress_struct c;
    struct jpeg_trap trap;
    unsigned char *buf = NULL;
    unsigned long size = 0;
    uint8_t *row = malloc((size_t)pic->w * 3), *out = NULL;
    int x;

    if (!row)
        return NULL;

    c.err = jpeg_std_error(&trap.mgr);
    trap.mgr.error_exit = jpeg_trap_exit;
    trap.mgr.output_message = jpeg_quiet;
    if (setjmp(trap.out))
    {
        jpeg_destroy_compress(&c);
        free(row);
        free(buf);
        return NULL;
    }

    jpeg_create_compress(&c);
    jpeg_mem_dest(&c, &buf, &size);
    c.image_width = pic->w;
    c.image_height = pic->h;
    c.input_components = 3;
    c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, quality, TRUE);
    jpeg_start_compress(&c, TRUE);

    if (nat_w != pic->w || nat_h != pic->h)
    {
        char note[48];

        snprintf(note, sizeof note, "webgate natural %dx%d", nat_w, nat_h);
        jpeg_write_marker(&c, JPEG_COM, (const JOCTET *)note, (unsigned)strlen(note));
    }

    while (c.next_scanline < c.image_height)
    {
        const uint8_t *p = pic->rgba + (size_t)c.next_scanline * pic->w * 4;
        JSAMPROW r = row;

        for (x = 0; x < pic->w; x++)
            memcpy(row + x * 3, p + x * 4, 3);
        jpeg_write_scanlines(&c, &r, 1);
    }

    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    free(row);

    /* Out of malloc's memory rather than libjpeg's, which is the same
     * allocator here but not a thing to rely on */
    out = malloc(size);
    if (out)
    {
        memcpy(out, buf, size);
        *len = size;
    }
    free(buf);

    return out;
}
#else
uint8_t *netpic_jpeg(const struct netpic *pic, int quality, int nat_w, int nat_h, size_t *len)
{
    (void)pic; (void)quality; (void)nat_w; (void)nat_h; (void)len;
    return NULL;
}
#endif

#ifdef HAVE_PNG
/* A chunk of tEXt put in after IHDR, which is the first chunk and is always
 * thirteen bytes: EWeb looks for the note before the pixels */
static uint8_t *add_note(uint8_t *png, size_t *len, int nat_w, int nat_h)
{
    char note[48];
    size_t n = (size_t)snprintf(note, sizeof note, "webgate%cnatural %dx%d", 0, nat_w, nat_h);
    size_t at = 8 + 12 + 13, chunk = 12 + n;
    uint8_t *out = malloc(*len + chunk);
    uint32_t crc;

    if (!out || *len < at)
    {
        free(out);
        return png;
    }

    memcpy(out, png, at);
    out[at + 0] = (uint8_t)(n >> 24);
    out[at + 1] = (uint8_t)(n >> 16);
    out[at + 2] = (uint8_t)(n >> 8);
    out[at + 3] = (uint8_t)n;
    memcpy(out + at + 4, "tEXt", 4);
    memcpy(out + at + 8, note, n);
    crc = netpic_crc32(out + at + 4, 4 + n);
    out[at + 8 + n + 0] = (uint8_t)(crc >> 24);
    out[at + 8 + n + 1] = (uint8_t)(crc >> 16);
    out[at + 8 + n + 2] = (uint8_t)(crc >> 8);
    out[at + 8 + n + 3] = (uint8_t)crc;
    memcpy(out + at + chunk, png + at, *len - at);

    free(png);
    *len += chunk;
    return out;
}

/*
 * A PNG of the picture's own colours where there are no more than 256 of
 * them, which is what a drawing made with few has and what a small machine
 * unpacks quickest; of every colour where there are more.
 */
uint8_t *netpic_png(const struct netpic *pic, int nat_w, int nat_h, size_t *len)
{
    png_image img;
    size_t n = (size_t)pic->w * pic->h, i, size = 0;
    uint8_t colours[256 * 4], *index = NULL, *out = NULL;
    const void *pixels = pic->rgba;
    int count = 0, j;

    index = malloc(n);
    if (index)
        for (i = 0; i < n && count <= 256; i++)
        {
            const uint8_t *p = pic->rgba + i * 4;

            for (j = 0; j < count; j++)
                if (memcmp(colours + j * 4, p, 4) == 0)
                    break;
            if (j == count && count++ < 256)
                memcpy(colours + j * 4, p, 4);
            index[i] = (uint8_t)j;
        }

    memset(&img, 0, sizeof img);
    img.version = PNG_IMAGE_VERSION;
    img.width = pic->w;
    img.height = pic->h;

    if (index && count <= 256)
    {
        img.format = PNG_FORMAT_RGBA_COLORMAP;
        img.colormap_entries = count;
        pixels = index;
    }
    else
        img.format = has_alpha(pic) ? PNG_FORMAT_RGBA : PNG_FORMAT_RGB;

    if (img.format == PNG_FORMAT_RGB)
    {
        /* Without its alpha, which libpng wants gone rather than ignored */
        uint8_t *rgb = malloc(n * 3);

        if (!rgb)
            goto done;
        for (i = 0; i < n; i++)
            memcpy(rgb + i * 3, pic->rgba + i * 4, 3);
        free(index);
        index = rgb;
        pixels = rgb;
    }

    if (!png_image_write_get_memory_size(img, size, 0, pixels, 0,
                                         img.format & PNG_FORMAT_FLAG_COLORMAP ? colours : NULL)
        || !(out = malloc(size))
        || !png_image_write_to_memory(&img, out, &size, 0, pixels, 0,
                                      img.format & PNG_FORMAT_FLAG_COLORMAP ? colours : NULL))
    {
        free(out);
        out = NULL;
        goto done;
    }

    *len = size;
    if (nat_w != pic->w || nat_h != pic->h)
        out = add_note(out, len, nat_w, nat_h);

done:
    free(index);
    return out;
}
#else
uint8_t *netpic_png(const struct netpic *pic, int nat_w, int nat_h, size_t *len)
{
    (void)pic; (void)nat_w; (void)nat_h; (void)len;
    return NULL;
}
#endif

/* Pens **********************************************************************/

static int nearest(const uint8_t *rgb, int n, int r, int g, int b)
{
    long best = -1;
    int i, at = 0;

    for (i = 0; i < n; i++)
    {
        long dr = r - rgb[i * 3], dg = g - rgb[i * 3 + 1], db = b - rgb[i * 3 + 2];
        long d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;

        if (best < 0 || d < best)
        {
            best = d;
            at = i;
        }
    }

    return at;
}

/*
 * EWPN: "EWPN", 1, the bits of a pen, flags (1: a mask follows), the number
 * of pens less one; the width and height, the picture's own width and
 * height, two bytes each; the colours' CRC. Then the pens a row at a time,
 * packed from the top bit, and the mask's rows after them, a bit a pixel and
 * set where it is shown. What img.c in EWeb unpacks.
 *
 * Dithered by Floyd and Steinberg's spreading of the error, one row each
 * way in turn so that it does not lean.
 */
uint8_t *netpic_pens(const struct netpic *pic, const uint8_t *rgb, int n, uint32_t crc,
                     int nat_w, int nat_h, size_t *len)
{
    int depth = n <= 2 ? 1 : n <= 4 ? 2 : n <= 16 ? 4 : 8;
    int mask = has_alpha(pic), w = pic->w, h = pic->h, x, y, k;
    size_t row = ((size_t)w * depth + 7) / 8, mrow = ((size_t)w + 7) / 8;
    size_t size = 20 + (size_t)h * (row + (mask ? mrow : 0));
    uint8_t *out = calloc(1, size), *pens, *bits;
    int *err = calloc((size_t)(w + 2) * 2 * 3, sizeof(int));
    int *now, *next;

    if (!out || !err)
    {
        free(out);
        free(err);
        return NULL;
    }

    memcpy(out, "EWPN\1", 5);
    out[5] = (uint8_t)depth;
    out[6] = (uint8_t)mask;
    out[7] = (uint8_t)(n - 1);
    out[8] = (uint8_t)(w >> 8);   out[9] = (uint8_t)w;
    out[10] = (uint8_t)(h >> 8);  out[11] = (uint8_t)h;
    out[12] = (uint8_t)(nat_w >> 8); out[13] = (uint8_t)nat_w;
    out[14] = (uint8_t)(nat_h >> 8); out[15] = (uint8_t)nat_h;
    out[16] = (uint8_t)(crc >> 24); out[17] = (uint8_t)(crc >> 16);
    out[18] = (uint8_t)(crc >> 8);  out[19] = (uint8_t)crc;

    pens = out + 20;
    bits = pens + (size_t)h * row;

    /* The error is kept in sixteenths, one pixel of margin each side */
    now = err;
    next = err + (w + 2) * 3;

    for (y = 0; y < h; y++)
    {
        int left = (y & 1) == 0, dir = left ? 1 : -1;

        memset(next, 0, (size_t)(w + 2) * 3 * sizeof(int));

        for (k = 0; k < w; k++)
        {
            int xx = left ? k : w - 1 - k;
            const uint8_t *p = pic->rgba + ((size_t)y * w + xx) * 4;
            int *e = now + (xx + 1) * 3, c[3], pen, i;

            if (p[3] < 128)
                continue;

            if (mask)
                bits[(size_t)y * mrow + xx / 8] |= (uint8_t)(0x80 >> (xx & 7));

            for (i = 0; i < 3; i++)
            {
                c[i] = p[i] + e[i] / 16;
                if (c[i] < 0)
                    c[i] = 0;
                if (c[i] > 255)
                    c[i] = 255;
            }

            pen = nearest(rgb, n, c[0], c[1], c[2]);

            for (i = 0; i < 3; i++)
            {
                int q = c[i] - rgb[pen * 3 + i];

                now[(xx + 1 + dir) * 3 + i] += q * 7;
                next[(xx + 1 - dir) * 3 + i] += q * 3;
                next[(xx + 1) * 3 + i] += q * 5;
                next[(xx + 1 + dir) * 3 + i] += q;
            }

            x = xx * depth;
            pens[(size_t)y * row + x / 8] |= (uint8_t)(pen << (8 - depth - (x & 7)));
        }

        {
            int *t = now;

            now = next;
            next = t;
        }
    }

    free(err);
    *len = size;
    return out;
}

/* What the Atari is given ***************************************************/

long netpic_budget(uint32_t free_st)
{
    long budget = (long)(free_st / 32) / 100 * 85;

    if (budget < 32L * 1024)
        budget = 32L * 1024;
    if (budget > 2048L * 1024)
        budget = 2048L * 1024;
    return budget;
}

/*
 * A drawing or a photograph written small: what is see-through as a PNG, a
 * photograph as a JPEG, and a drawing - a PNG or a WebP to start with - as
 * whichever of the two is smaller, which for a few flat colours is the PNG
 * by far and for a photograph saved as a PNG is the JPEG by as much.
 */
static uint8_t *compress(const struct netpic *pic, int kind, int quality, int nat_w, int nat_h,
                         size_t *len, int *png)
{
    uint8_t *out, *other = NULL;
    size_t other_len = 0;

    *png = has_alpha(pic);
    if (*png)
        return netpic_png(pic, nat_w, nat_h, len);

    out = netpic_jpeg(pic, quality, nat_w, nat_h, len);
    if (kind != NETPIC_JPEG)
        other = netpic_png(pic, nat_w, nat_h, &other_len);
    if (other && (!out || other_len < *len))
    {
        free(out);
        *len = other_len;
        *png = 1;
        return other;
    }

    free(other);
    return out;
}

int netpic_convert(const struct netpic_ask *ask, const uint8_t *data, size_t len,
                   uint8_t **out, size_t *outlen, const char **type,
                   char *what, size_t whatsize)
{
    uint8_t rgb[256 * 3];
    struct netpic pic, made;
    int kind = netpic_kind(data, len), own_w = 0, own_h = 0, w, h, n = 0, png;
    int quality, first;
    size_t pens_len = 0;
    long want, budget = ask->budget > 0 ? ask->budget : NETPIC_BUDGET;

    *out = NULL;

    if (ask->pens)
    {
        n = palette_get(ask->pal, rgb);
        if (n == 0)
            return NETPIC_NEED_PAL;
        if (n != ask->pens)
            return NETPIC_ASIS;
    }

    if (kind != NETPIC_JPEG && kind != NETPIC_PNG && kind != NETPIC_WEBP)
        return NETPIC_ASIS;

    want = ask->pens && ask->w ? ask->w : ask->fit;
    if (decode(data, len, want, &pic, &own_w, &own_h) < 0)
        return NETPIC_ASIS;

    /* One that the Atari reads as it is, that fits, and that it will take */
    if (!ask->pens && kind != NETPIC_WEBP && own_w <= ask->fit && (long)len <= budget
        && !(kind == NETPIC_JPEG && netpic_progressive(data, len)))
    {
        netpic_free(&pic);
        return NETPIC_ASIS;
    }

    if (ask->pens && ask->w)
    {
        w = ask->w;
        h = ask->h;
    }
    else if (own_w > ask->fit)
    {
        w = (int)ask->fit;
        h = (int)(((long)own_h * ask->fit + own_w / 2) / own_w);
        if (h < 1)
            h = 1;
    }
    else
    {
        w = own_w;
        h = own_h;
    }

    if (w != pic.w || h != pic.h)
    {
        if (netpic_scale(&pic, w, h, &made) < 0)
        {
            netpic_free(&pic);
            return NETPIC_ASIS;
        }
        netpic_free(&pic);
        pic = made;
    }

    if (ask->pens)
    {
        *out = netpic_pens(&pic, rgb, n, ask->pal, own_w, own_h, outlen);
        if (*out && (long)*outlen <= budget)
        {
            *type = "application/x-aera-pens";
            snprintf(what, whatsize, "%s %dx%d to pens %dx%d", kind_name(kind), own_w, own_h, w, h);
            netpic_free(&pic);
            return NETPIC_MADE;
        }

        /* Pens are a byte for two pixels however plain the picture, and a
         * large one is more than EWeb takes: it has the picture written
         * small instead, and dithers it itself */
        pens_len = *out ? *outlen : 0;
        free(*out);
        *out = NULL;
    }

    /*
     * Within what EWeb takes: the quality let down a step at a time, then the
     * picture made smaller, which it is shown larger again from - the size
     * it had is in it.
     */
    quality = first = ask->deep ? 90 : 80;
    for (;;)
    {
        *out = compress(&pic, kind, quality, own_w, own_h, outlen, &png);
        if (!*out || (long)*outlen <= budget)
            break;

        if (!png && quality > 50)
            quality -= 10;
        else if (pic.w >= 64 && pic.h >= 16
                 && netpic_scale(&pic, pic.w * 3 / 4, pic.h * 3 / 4, &made) == 0)
        {
            netpic_free(&pic);
            pic = made;
        }
        else
            break;

        free(*out);
        *out = NULL;
    }

    *type = png ? "image/png" : "image/jpeg";
    n = snprintf(what, whatsize, "%s %dx%d to %s %dx%d", kind_name(kind), own_w, own_h,
                 png ? "PNG" : "JPEG", pic.w, pic.h);
    if (quality != first && n > 0 && (size_t)n < whatsize)
        n += snprintf(what + n, whatsize - n, ", quality %d", quality);
    if (pens_len && n > 0 && (size_t)n < whatsize)
        snprintf(what + n, whatsize - n, ", not pens: %zu KB", pens_len / 1024);

    netpic_free(&pic);

    return *out ? NETPIC_MADE : NETPIC_ASIS;
}
