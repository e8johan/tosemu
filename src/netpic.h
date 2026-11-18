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
 * The pictures the built-in gateway makes for EWeb, as webgate makes them -
 * see AERA-Gateway's docs/SERVICES.md, "Pictures":
 *
 *   X-Gateway-Picture: fit=608
 *   X-Gateway-Picture: fit=608; size=400x300; pens=16; pal=1a2b3c4d; ed=tj
 *
 * The first asks for a picture no wider than the page, and in a form the
 * Atari reads: a WebP, a progressive JPEG or one too large comes back as a
 * baseline JPEG, or a PNG where it is transparent. The second asks for the
 * screen's own pens at the size the picture is shown, dithered here rather
 * than by a 68000, in EWeb's EWPN form; the colours were sent beforehand by
 * a POST to http://webgate/palette and are named by their CRC.
 *
 * A picture made smaller says how large it was, which is what keeps the page
 * laid out as its author meant: "webgate natural WxH" in a JPEG's comment, in
 * a PNG's tEXt under the keyword "webgate", and in the pens' own head.
 */

#ifndef NETPIC_H
#define NETPIC_H

#include <stddef.h>
#include <stdint.h>

enum { NETPIC_OTHER, NETPIC_JPEG, NETPIC_PNG, NETPIC_GIF, NETPIC_WEBP };

/* What X-Gateway-Picture asked for */
struct netpic_ask
{
    long fit;           /* No wider than this */
    int deep;           /* A true colour screen: JPEG of quality 90 */
    int pens;           /* As this many of the screen's pens, or 0 */
    uint32_t pal;       /* The pens' colours, by their CRC */
    int w, h;           /* The size to make the pens at, or 0 */
    long budget;        /* The most to send, or 0 for NETPIC_BUDGET */
};

/* 0, or -1 for a value that is not one */
int netpic_parse_ask(const char *value, struct netpic_ask *ask);

int netpic_kind(const uint8_t *data, size_t len);

uint32_t netpic_crc32(const uint8_t *data, size_t len);

/* Keeps a palette of 2 to 256 colours, three bytes each. Its CRC, or 0 for
 * colours that are not a palette. */
uint32_t netpic_palette_add(const uint8_t *rgb, size_t len);
int netpic_palette_known(uint32_t crc);

/*
 * The most a picture is sent as. EWeb takes a picture of at most a
 * thirty-second of the ST RAM that was free when it started - more on a
 * machine with TT RAM, which this is not - and refuses a larger one whole.
 * Pens larger than the budget go as a JPEG or PNG, and those are written
 * smaller until they are no larger than it.
 *
 * What was free when it started is about what was free when it first came to
 * the gateway, which GEMDOS keeps - see mem_free_first_asked - so a
 * thirty-second of that, less a margin, is a budget it will take:
 * netpic_budget. NETPIC_BUDGET is for when nothing better is known.
 */
#define NETPIC_BUDGET (128L * 1024)

long netpic_budget(uint32_t free_st);

/* What netpic_convert did */
#define NETPIC_ASIS     (0)   /* Nothing: the picture goes as it came */
#define NETPIC_MADE     (1)   /* *out is the picture to send instead */
#define NETPIC_NEED_PAL (2)   /* Pens of colours not sent yet: a 409 */

/*
 * The picture the Atari is to be given for one it asked for with ask. With
 * NETPIC_MADE, *out (malloc'd) and its length, its Content-Type, and what was
 * done in what, as X-Gateway-Picture answers it.
 */
int netpic_convert(const struct netpic_ask *ask, const uint8_t *data, size_t len,
                   uint8_t **out, size_t *outlen, const char **type,
                   char *what, size_t whatsize);

/* The pieces, for netpictest.c *********************************************/

/* A picture, eight bits each of red, green, blue and alpha */
struct netpic
{
    int w, h;
    uint8_t *rgba;
};

int netpic_decode(const uint8_t *data, size_t len, long want_w, struct netpic *pic);
int netpic_scale(const struct netpic *in, int w, int h, struct netpic *out);
void netpic_free(struct netpic *pic);

/* The natural size goes in each, when it is not the picture's own */
uint8_t *netpic_jpeg(const struct netpic *pic, int quality, int nat_w, int nat_h, size_t *len);
uint8_t *netpic_png(const struct netpic *pic, int nat_w, int nat_h, size_t *len);
uint8_t *netpic_pens(const struct netpic *pic, const uint8_t *rgb, int n, uint32_t crc,
                     int nat_w, int nat_h, size_t *len);

/* Whether a JPEG is progressive, which EWeb shows only the averages of */
int netpic_progressive(const uint8_t *data, size_t len);

#endif /* NETPIC_H */
