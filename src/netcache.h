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
 * The pictures the gateway has fetched, kept a few minutes by their address.
 *
 * EWeb asks for a picture more than once - at the page's width, and again at
 * the size it turns out to be shown at - and names a page's pictures ahead so
 * that they can be fetched a few at a time. Each of those is a trip to a
 * server that may be slow, and the answer is the same each time; what is
 * made of it for the Atari is not, so the answer is kept as it came and made
 * over for each request.
 *
 * Shared by the gateway's threads. A picture being fetched is waited for
 * rather than fetched twice.
 */

#ifndef NETCACHE_H
#define NETCACHE_H

#include <stddef.h>
#include <stdint.h>

/* An answer as the server gave it, its body whole and unchunked */
struct fetched
{
    char *head;         /* NUL terminated */
    uint8_t *body;
    size_t len;
    char tls[128];      /* How it was reached, or "" */
    int refs;
};

/* A new answer, with one reference, the caller's. NULL without memory. */
struct fetched *fetched_new(const char *head, size_t head_len, uint8_t *body, size_t len,
                            const char *tls);
void fetched_release(struct fetched *f);

/*
 * Claims an address for fetching. 1 when the caller is to fetch it and say
 * how it went with netcache_done; 0 when it is kept or being fetched already.
 */
int netcache_claim(const char *url);

/* The end of a claim: f is kept (a reference of the cache's own is taken),
 * or NULL for a fetch that failed, which those waiting are told */
void netcache_done(const char *url, struct fetched *f);

/* What is kept for an address, waiting up to wait_ms while it is being
 * fetched: a reference to let go of with fetched_release, or NULL */
struct fetched *netcache_get(const char *url, long wait_ms);

/* For the tests: how much is kept at most, and how long. Nought is the
 * default. And everything let go of. */
void netcache_limits(size_t bytes, long ms);
void netcache_clear(void);

#endif /* NETCACHE_H */
