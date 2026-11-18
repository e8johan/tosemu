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
 * A gateway of the kind AERA-Gateway's webgate is, for EWeb's ST edition,
 * which has no TLS of its own: it takes requests in proxy form, makes the
 * secure connection on the host, and hands back plain HTTP. Pictures are made
 * over the way webgate makes them - see netpic.h - and fetched ahead and kept
 * - see netcache.h. Snapshots and translation are not offered, and EWeb does
 * without them when the answers do not say they were done.
 */

#ifndef NETGATE_H
#define NETGATE_H

#include <stddef.h>
#include <stdint.h>

#define NETGATE_TARGET_MAX (8192)

struct netgate_target
{
    char method[16];
    int tls;
    char host[256];
    unsigned port;
    char target[NETGATE_TARGET_MAX];
};

/* What netgate_parse_request_line found */
#define NETGATE_BAD    (-1)
#define NETGATE_ORIGIN (0)  /* GET /path, asked of the gateway itself */
#define NETGATE_PROXY  (1)  /* GET http[s]://host[:port]/path */

int netgate_enabled(void);

/* Serves one connection from the emulated machine on a thread of its own,
 * which closes fd when it is done. budget is the most a picture is sent as -
 * see netpic_budget. 0, or -1 if no thread could be started. */
int netgate_serve(int fd, long budget);

/* The first line of a request head */
int netgate_parse_request_line(const char *line, struct netgate_target *t);

/* A request's Content-Length: -1 without one, -2 for a body this cannot
 * pass on (chunked, or a length that is not a number) */
long netgate_content_length(const char *head);

/* The request head to send upstream, in origin form and asking for the
 * connection to close. Its length, or -1 if out is too small. */
long netgate_request_head(const char *head, const struct netgate_target *t,
                          char *out, size_t size);

/* The answer's head as the program is given it: the connection closes after
 * it, and tls, if not NULL, goes in X-Gateway-TLS. The headers named in drop
 * (a NULL-terminated list, or NULL) are left out, and the lines in extra put
 * in. Its length, or -1. */
long netgate_response_head(const char *head, const char *tls, const char *const *drop,
                           const char *extra, char *out, size_t size);

/* A header's value, trimmed, into value. Whether there was one. */
int netgate_header(const char *head, const char *name, char *value, size_t size);

/* An answer head's status code, or -1 */
int netgate_status(const char *head);

/* A line of a list of pictures to make ahead */
struct netgate_prefetch
{
    const char *url, *ask, *cookie;
};

/* Splits such a list in place - a NUL terminated body - into its lines, at
 * most most of them, and the page's Referer and Accept, "" where it has none.
 * How many lines. */
int netgate_prefetch_lines(char *body, const char **referer, const char **accept,
                           struct netgate_prefetch *lines, int most);

/* A chunked body as the bytes it carries, in place. Its length, or -1 for one
 * that is not whole. */
long netgate_dechunk(uint8_t *data, size_t len);

#endif /* NETGATE_H */
