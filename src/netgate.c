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
 * One request per connection. EWeb keeps a connection to its gateway open
 * between requests, but every answer here says Connection: close, so it
 * opens another for the next one - and the answer's body can go through as
 * bytes, without being read for where it ends. A picture asked for with
 * X-Gateway-Picture is the exception: that is taken in whole, to be made over
 * and kept a while - see netcache.h - and so are the pictures a page names
 * ahead, a few at a time.
 *
 * The threads touch nothing of the emulated machine: they have a socket to the
 * program on one side and one to the world on the other.
 */

#include "netgate.h"

#include "netcache.h"
#include "netpic.h"
#include "settings.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <netdb.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef HAVE_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

#define HEAD_MAX  (16384)
#define IO_SECONDS (60)

/* The most of a picture taken in to be made over, which is webgate's */
#define PICTURE_MAX (40L * 1024 * 1024)

int netgate_enabled(void)
{
    return !setting_flag("TOSEMU_NO_GATEWAY");
}

/*
 * TOSEMU_TRACE_GATEWAY says what each request was and where its time went:
 * a picture from what was kept, waited for while it was fetched ahead, or
 * fetched for itself - which is the question a slow page asks. Read once, by
 * the emulator's own thread, before the first request is served.
 */
static int tracing;

static double seconds(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void trace(const char *format, ...)
{
    va_list a;

    if (!tracing)
        return;

    va_start(a, format);
    fprintf(stderr, "tosemu gateway: ");
    vfprintf(stderr, format, a);
    fprintf(stderr, "\n");
    va_end(a);
}

/* Parsing ********************************************************************/

/* The end of the line starting at p, before its CR LF or LF */
static const char *line_end(const char *p)
{
    while (*p && *p != '\r' && *p != '\n')
        p++;
    return p;
}

/* The line after the one starting at p, or NULL at the end of the head */
static const char *next_line(const char *p)
{
    p = line_end(p);

    if (*p == '\r')
        p++;
    if (*p == '\n')
        p++;

    return *p && *p != '\r' && *p != '\n' ? p : NULL;
}

static int is_header(const char *line, const char *name)
{
    size_t n = strlen(name);

    return strncasecmp(line, name, n) == 0 && line[n] == ':';
}

int netgate_parse_request_line(const char *line, struct netgate_target *t)
{
    const char *end = line_end(line);
    const char *p = line, *uri, *uri_end, *host, *host_end, *colon = NULL;
    size_t n;

    memset(t, 0, sizeof *t);

    while (p < end && isupper((unsigned char)*p))
        p++;
    n = p - line;
    if (n == 0 || n >= sizeof t->method || *p != ' ')
        return NETGATE_BAD;
    memcpy(t->method, line, n);

    uri = ++p;
    while (p < end && *p != ' ')
        p++;
    uri_end = p;
    if (uri_end == uri || *p != ' ')
        return NETGATE_BAD;

    if (end - (p + 1) != 8 || (strncmp(p + 1, "HTTP/1.1", 8) != 0
                               && strncmp(p + 1, "HTTP/1.0", 8) != 0))
        return NETGATE_BAD;

    if (*uri == '/')
    {
        n = uri_end - uri;
        if (n >= sizeof t->target)
            return NETGATE_BAD;
        memcpy(t->target, uri, n);
        return NETGATE_ORIGIN;
    }

    if (uri_end - uri > 7 && strncasecmp(uri, "http://", 7) == 0)
        host = uri + 7;
    else if (uri_end - uri > 8 && strncasecmp(uri, "https://", 8) == 0)
    {
        host = uri + 8;
        t->tls = 1;
    }
    else
        return NETGATE_BAD;

    for (host_end = host; host_end < uri_end && *host_end != '/' && *host_end != '?';
         host_end++)
    {
        if (*host_end == ':')
            colon = host_end;
        else if (!isalnum((unsigned char)*host_end) && *host_end != '.'
                 && *host_end != '-' && *host_end != '_')
            return NETGATE_BAD;     /* user info, an IPv6 literal, rubbish */
    }

    if (colon)
    {
        unsigned long port = 0;
        const char *d;

        if (colon + 1 == host_end || host_end - colon > 6)
            return NETGATE_BAD;
        for (d = colon + 1; d < host_end; d++)
        {
            if (!isdigit((unsigned char)*d))
                return NETGATE_BAD;
            port = port * 10 + (*d - '0');
        }
        if (port == 0 || port > 65535)
            return NETGATE_BAD;
        t->port = (unsigned)port;
    }
    else
    {
        colon = host_end;
        t->port = t->tls ? 443 : 80;
    }

    n = colon - host;
    if (n == 0 || n >= sizeof t->host)
        return NETGATE_BAD;
    memcpy(t->host, host, n);

    n = uri_end - host_end;
    if (n + 2 > sizeof t->target)
        return NETGATE_BAD;
    if (*host_end != '/')
        t->target[0] = '/';
    memcpy(t->target + strlen(t->target), host_end, n);

    return NETGATE_PROXY;
}

long netgate_content_length(const char *head)
{
    const char *line;
    long len = -1;

    for (line = next_line(head); line; line = next_line(line))
    {
        if (is_header(line, "Transfer-Encoding"))
            return -2;

        if (is_header(line, "Content-Length"))
        {
            const char *p = line + strlen("Content-Length:"), *end = line_end(line);
            char *stop;

            while (p < end && (*p == ' ' || *p == '\t'))
                p++;
            if (!isdigit((unsigned char)*p))
                return -2;
            len = strtol(p, &stop, 10);
            while (stop < end && (*stop == ' ' || *stop == '\t'))
                stop++;
            if (stop != end || len < 0)
                return -2;
        }
    }

    return len;
}

/* Appends to out, keeping count in *at. 0, or -1 when it does not fit. */
static int add(char *out, size_t size, size_t *at, const char *s, size_t n)
{
    if (*at + n + 1 > size)
        return -1;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return 0;
}

/* What is between the two ends, and nobody else's business */
static int hop_by_hop(const char *line)
{
    return is_header(line, "Connection") || is_header(line, "Keep-Alive")
        || is_header(line, "Proxy-Connection") || is_header(line, "Proxy-Authorization")
        || is_header(line, "Proxy-Authenticate") || strncasecmp(line, "X-Gateway-", 10) == 0;
}

/* The head's header lines, less the hop-by-hop ones. Whether there is a Host
 * among them goes in *host. */
static int add_headers(const char *head, char *out, size_t size, size_t *at, int *host)
{
    const char *line;

    for (line = next_line(head); line; line = next_line(line))
    {
        if (hop_by_hop(line))
            continue;
        if (host && is_header(line, "Host"))
            *host = 1;
        if (add(out, size, at, line, line_end(line) - line) < 0
            || add(out, size, at, "\r\n", 2) < 0)
            return -1;
    }

    return 0;
}

long netgate_request_head(const char *head, const struct netgate_target *t,
                          char *out, size_t size)
{
    char buf[64 + sizeof t->host];
    size_t at = 0;
    int host = 0;

    snprintf(buf, sizeof buf, "%s ", t->method);
    if (add(out, size, &at, buf, strlen(buf)) < 0
        || add(out, size, &at, t->target, strlen(t->target)) < 0
        || add(out, size, &at, " HTTP/1.1\r\n", 11) < 0
        || add_headers(head, out, size, &at, &host) < 0)
        return -1;

    if (!host)
    {
        if (t->port == (t->tls ? 443u : 80u))
            snprintf(buf, sizeof buf, "Host: %s\r\n", t->host);
        else
            snprintf(buf, sizeof buf, "Host: %s:%u\r\n", t->host, t->port);
        if (add(out, size, &at, buf, strlen(buf)) < 0)
            return -1;
    }

    if (add(out, size, &at, "Connection: close\r\n\r\n", 21) < 0)
        return -1;

    return (long)at;
}

int netgate_header(const char *head, const char *name, char *value, size_t size)
{
    const char *line;

    for (line = next_line(head); line; line = next_line(line))
        if (is_header(line, name))
        {
            const char *p = line + strlen(name) + 1, *end = line_end(line);
            size_t n;

            while (p < end && (*p == ' ' || *p == '\t'))
                p++;
            while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
                end--;
            n = end - p;
            if (n >= size)
                n = size - 1;
            memcpy(value, p, n);
            value[n] = 0;
            return 1;
        }

    return 0;
}

int netgate_status(const char *head)
{
    if (strncmp(head, "HTTP/1.", 7) != 0 || head[8] != ' ' || !isdigit((unsigned char)head[9]))
        return -1;
    return (int)strtol(head + 9, NULL, 10);
}

long netgate_dechunk(uint8_t *d, size_t n)
{
    size_t in = 0, out = 0;

    for (;;)
    {
        unsigned long size = 0;
        int digits = 0;

        while (in < n && isxdigit(d[in]))
        {
            int c = d[in++];

            size = size * 16 + (isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10));
            if (++digits > 8)
                return -1;
        }
        if (!digits)
            return -1;
        /* An extension, then the line's end */
        while (in < n && d[in] != '\n')
            in++;
        if (in++ >= n)
            return -1;

        if (size == 0)
            return (long)out;
        if (size > n - in)
            return -1;

        memmove(d + out, d + in, size);
        out += size;
        in += size;

        if (in < n && d[in] == '\r')
            in++;
        if (in >= n || d[in] != '\n')
            return -1;
        in++;
    }
}

/* Whether a line is one of the headers named */
static int named(const char *line, const char *const *names)
{
    for (; names && *names; names++)
        if (is_header(line, *names))
            return 1;
    return 0;
}

long netgate_response_head(const char *head, const char *tls, const char *const *drop,
                           const char *extra, char *out, size_t size)
{
    const char *line;
    size_t at = 0;

    if (add(out, size, &at, head, line_end(head) - head) < 0
        || add(out, size, &at, "\r\n", 2) < 0)
        return -1;

    for (line = next_line(head); line; line = next_line(line))
    {
        if (hop_by_hop(line) || named(line, drop))
            continue;
        if (add(out, size, &at, line, line_end(line) - line) < 0
            || add(out, size, &at, "\r\n", 2) < 0)
            return -1;
    }

    if (extra && add(out, size, &at, extra, strlen(extra)) < 0)
        return -1;

    if (tls && (add(out, size, &at, "X-Gateway-TLS: ", 15) < 0
                || add(out, size, &at, tls, strlen(tls)) < 0
                || add(out, size, &at, "\r\n", 2) < 0))
        return -1;

    if (add(out, size, &at, "Connection: close\r\n\r\n", 21) < 0)
        return -1;

    return (long)at;
}

/* Serving ********************************************************************/

/* The far side: a socket, and the TLS on it when there is any */
struct upstream
{
    int fd;
#ifdef HAVE_OPENSSL
    SSL *ssl;
#endif
};

static int send_all(int fd, const char *p, size_t n)
{
    while (n > 0)
    {
        ssize_t r = send(fd, p, n, MSG_NOSIGNAL);

        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        p += r;
        n -= r;
    }
    return 0;
}

static ssize_t up_read(struct upstream *u, char *p, size_t n)
{
#ifdef HAVE_OPENSSL
    if (u->ssl)
    {
        int r = SSL_read(u->ssl, p, n > INT32_MAX ? INT32_MAX : (int)n);

        return r > 0 ? r : (SSL_get_error(u->ssl, r) == SSL_ERROR_ZERO_RETURN ? 0 : -1);
    }
#endif
    for (;;)
    {
        ssize_t r = recv(u->fd, p, n, 0);

        if (r >= 0 || errno != EINTR)
            return r;
    }
}

static int up_write(struct upstream *u, const char *p, size_t n)
{
#ifdef HAVE_OPENSSL
    if (u->ssl)
    {
        while (n > 0)
        {
            int r = SSL_write(u->ssl, p, n > INT32_MAX ? INT32_MAX : (int)n);

            if (r <= 0)
                return -1;
            p += r;
            n -= r;
        }
        return 0;
    }
#endif
    return send_all(u->fd, p, n);
}

static void timeouts(int fd)
{
    struct timeval tv = { IO_SECONDS, 0 };

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

/* An answer of the gateway's own, with any headers of its own in extra */
static void answer_with(int fd, const char *status, const char *type, const char *extra,
                        const char *text)
{
    char buf[1024];
    int n;

    n = snprintf(buf, sizeof buf,
                 "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n%s"
                 "Connection: close\r\n\r\n%s",
                 status, type, strlen(text), extra ? extra : "", text);
    if (n > 0)
        send_all(fd, buf, n < (int)sizeof buf ? (size_t)n : sizeof buf - 1);
}

static void answer(int fd, const char *status, const char *type, const char *text)
{
    answer_with(fd, status, type, NULL, text);
}

static void refuse(int fd, const char *status, const char *why)
{
    char text[600];

    snprintf(text, sizeof text,
             "<!DOCTYPE html><title>webgate</title><p>The gateway: %s</p>\n", why);
    answer(fd, status, "text/html", text);
}

/* Reads up to the blank line that ends a head, into buf, NUL terminated.
 * How much was read, head and anything after it, or -1. */
static ssize_t read_head(int fd, struct upstream *u, char *buf, size_t size,
                         char **end)
{
    size_t got = 0;

    for (;;)
    {
        ssize_t r;
        char *e;

        if (got + 1 >= size)
            return -1;

        r = u ? up_read(u, buf + got, size - 1 - got) : recv(fd, buf + got, size - 1 - got, 0);
        if (r < 0 && errno == EINTR && !u)
            continue;
        if (r <= 0)
            return -1;
        got += r;
        buf[got] = 0;

        if ((e = strstr(buf, "\r\n\r\n")) != NULL)
        {
            *end = e + 4;
            return got;
        }
        if ((e = strstr(buf, "\n\n")) != NULL)
        {
            *end = e + 2;
            return got;
        }
    }
}

static int connect_upstream(const char *host, unsigned port, char *why, size_t whysize)
{
    struct addrinfo hints, *res, *ai;
    char service[8];
    int fd = -1, err;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof service, "%u", port);

    if ((err = getaddrinfo(host, service, &hints, &res)) != 0)
    {
        snprintf(why, whysize, "can't find %s (%s)", host, gai_strerror(err));
        return -1;
    }

    err = 0;
    for (ai = res; ai; ai = ai->ai_next)
    {
        fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0)
            continue;
        timeouts(fd);
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        err = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0)
        snprintf(why, whysize, "can't connect to %s:%u (%s)", host, port,
                 strerror(err ? err : ECONNREFUSED));

    return fd;
}

#ifdef HAVE_OPENSSL
static SSL_CTX *tls_ctx;
static pthread_once_t tls_once = PTHREAD_ONCE_INIT;

static void tls_init(void)
{
    tls_ctx = SSL_CTX_new(TLS_client_method());
    if (!tls_ctx)
        return;

    SSL_CTX_set_min_proto_version(tls_ctx, TLS1_2_VERSION);
    SSL_CTX_set_default_verify_paths(tls_ctx);
    SSL_CTX_set_verify(tls_ctx, SSL_VERIFY_PEER, NULL);
    /* Plenty of servers close without saying so, after a body whose end
     * was known anyway */
    SSL_CTX_set_options(tls_ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
}

static int is_ipv4(const char *host)
{
    struct in_addr a;

    return inet_pton(AF_INET, host, &a) == 1;
}

/* TLS on u's socket, with what it was in info ("TLS 1.3, <cipher>") */
static int tls_start(struct upstream *u, const char *host, char *info, size_t infosize,
                     char *why, size_t whysize)
{
    const char *v;
    long verify;

    pthread_once(&tls_once, tls_init);
    if (!tls_ctx || !(u->ssl = SSL_new(tls_ctx)))
    {
        snprintf(why, whysize, "no TLS on this host");
        return -1;
    }

    SSL_set_fd(u->ssl, u->fd);
    if (is_ipv4(host))
        X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(u->ssl), host);
    else
    {
        SSL_set_tlsext_host_name(u->ssl, host);
        SSL_set1_host(u->ssl, host);
    }

    if (SSL_connect(u->ssl) != 1)
    {
        verify = SSL_get_verify_result(u->ssl);
        if (verify != X509_V_OK)
            snprintf(why, whysize, "%s's certificate: %s", host,
                     X509_verify_cert_error_string(verify));
        else
        {
            const char *reason = ERR_reason_error_string(ERR_get_error());

            snprintf(why, whysize, "no secure connection to %s (%s)", host,
                     reason ? reason : "it closed");
        }
        return -1;
    }

    v = SSL_get_version(u->ssl);
    if (strncmp(v, "TLSv", 4) == 0)
        snprintf(info, infosize, "TLS %s, %s", v + 4, SSL_get_cipher_name(u->ssl));
    else
        snprintf(info, infosize, "%s, %s", v, SSL_get_cipher_name(u->ssl));

    return 0;
}
#endif

/* A request's body, all of it, which the gateway's own calls have: what came
 * with the head and the rest of its length. NULL for one too long. */
static uint8_t *request_body(int fd, const char *left, size_t nleft, long len, size_t most)
{
    uint8_t *body;
    size_t have;

    if (len < 0 || (size_t)len > most || !(body = malloc(len ? len : 1)))
        return NULL;

    have = nleft < (size_t)len ? nleft : (size_t)len;
    memcpy(body, left, have);

    while (have < (size_t)len)
    {
        ssize_t r = recv(fd, body + have, len - have, 0);

        if (r <= 0)
        {
            free(body);
            return NULL;
        }
        have += r;
    }

    return body;
}

/* Picture answers, whole ******************************************************/

/* An address as the cache knows it: the same picture by whichever way it
 * was named */
static void cache_key(const struct netgate_target *t, char *key, size_t size)
{
    snprintf(key, size, "%s://%s:%u%s", t->tls ? "https" : "http", t->host, t->port, t->target);
}

/*
 * A request made and its answer taken in whole: its head, and its body
 * unchunked. The upstream connection is the request's own and goes with it.
 * NULL with why for one that could not be had, or that is larger than a
 * picture is let be.
 */
static struct fetched *fetch_whole(const struct netgate_target *t, const char *req, size_t req_len,
                                   char *why, size_t whysize)
{
    struct upstream u = { -1 };
    struct fetched *f = NULL;
    char tls[128] = "", value[64], *head = malloc(HEAD_MAX), *end;
    uint8_t *raw = NULL;
    size_t raw_len = 0, raw_size = 0, nleft;
    const char *left;
    long clen, blen;
    ssize_t got;
    int chunked;

    if (!head)
    {
        snprintf(why, whysize, "no memory");
        return NULL;
    }

#ifndef HAVE_OPENSSL
    if (t->tls)
    {
        snprintf(why, whysize, "this tosemu was built without OpenSSL, so it has no TLS");
        goto done;
    }
#endif

    u.fd = connect_upstream(t->host, t->port, why, whysize);
    if (u.fd < 0)
        goto done;

#ifdef HAVE_OPENSSL
    if (t->tls && tls_start(&u, t->host, tls, sizeof tls, why, whysize) < 0)
        goto done;
#endif

    if (up_write(&u, req, req_len) < 0)
    {
        snprintf(why, whysize, "the request could not be sent");
        goto done;
    }

    got = read_head(-1, &u, head, HEAD_MAX, &end);
    if (got < 0)
    {
        snprintf(why, whysize, "no answer from %s", t->host);
        goto done;
    }

    clen = netgate_content_length(head);
    chunked = netgate_header(head, "Transfer-Encoding", value, sizeof value)
              && strstr(value, "chunked") != NULL;
    left = end;
    nleft = got - (end - head);

    /* All of it: to its length, or to the close, which was asked for */
    for (;;)
    {
        ssize_t n;

        if (raw_len == raw_size)
        {
            size_t grow = raw_size ? raw_size * 2 : 65536;
            uint8_t *more = realloc(raw, grow);

            if (!more)
            {
                snprintf(why, whysize, "no memory");
                goto done;
            }
            raw = more;
            raw_size = grow;
        }

        if (nleft)
        {
            n = nleft < raw_size - raw_len ? nleft : raw_size - raw_len;
            memcpy(raw + raw_len, left, n);
            left += n;
            nleft -= n;
        }
        else
        {
            if (clen >= 0 && (long)raw_len >= clen)
                break;
            n = up_read(&u, (char *)raw + raw_len, raw_size - raw_len);
            if (n <= 0)
                break;
        }

        raw_len += n;
        if (raw_len > PICTURE_MAX)
        {
            snprintf(why, whysize, "the picture from %s is too big", t->host);
            goto done;
        }
    }

    blen = (long)raw_len;
    if (chunked && (blen = netgate_dechunk(raw, raw_len)) < 0)
    {
        snprintf(why, whysize, "the answer from %s was cut short", t->host);
        goto done;
    }
    if (clen >= 0 && blen > clen)
        blen = clen;

    f = fetched_new(head, end - head, raw, blen, tls);
    if (f)
        raw = NULL;
    else
        snprintf(why, whysize, "no memory");

done:
#ifdef HAVE_OPENSSL
    if (u.ssl)
        SSL_free(u.ssl);
#endif
    if (u.fd >= 0)
        close(u.fd);
    free(raw);
    free(head);
    return f;
}

/*
 * An answer taken in whole, as the program is given it: made over for ask
 * where it is a picture that is, as it came otherwise. Sent compressed, it
 * goes as it came too, which no server does to a picture worth the trouble.
 */
static void send_fetched(int fd, const struct fetched *f, const struct netpic_ask *ask,
                         char *said, size_t saidsize)
{
    static const char *const body_headers[] = {
        "Content-Length", "Transfer-Encoding", NULL
    };
    static const char *const picture_headers[] = {
        "Content-Length", "Transfer-Encoding", "Content-Type", NULL
    };
    char extra[256], what[128], value[64], *out = malloc(HEAD_MAX + 512);
    const char *tls = f->tls[0] ? f->tls : NULL, *type = NULL;
    uint8_t *made = NULL;
    size_t made_len = 0;
    long n;

    snprintf(said, saidsize, "%d, %zu bytes as it came", netgate_status(f->head), f->len);

    if (!out)
        return;

    if (ask && netgate_status(f->head) == 200
        && !(netgate_header(f->head, "Content-Encoding", value, sizeof value)
             && strcasecmp(value, "identity") != 0))
    {
        int r = netpic_convert(ask, f->body, f->len, &made, &made_len, &type, what, sizeof what);

        if (r == NETPIC_NEED_PAL)
        {
            snprintf(said, saidsize, "409, colours needed");
            answer_with(fd, "409 Conflict", "text/plain", "X-Gateway-Palette: need\r\n",
                        "webgate: send the colours first\n");
            goto done;
        }

        if (r == NETPIC_MADE)
        {
            snprintf(said, saidsize, "%s, %zu bytes", what, made_len);
            snprintf(extra, sizeof extra,
                     "Content-Type: %s\r\nContent-Length: %zu\r\nX-Gateway-Picture: %s\r\n%s",
                     type, made_len, what, ask->pens ? "X-Gateway-Pens: tosemu\r\n" : "");
            n = netgate_response_head(f->head, tls, picture_headers, extra, out, HEAD_MAX + 512);
            if (n >= 0 && send_all(fd, out, n) == 0)
                send_all(fd, (const char *)made, made_len);
            goto done;
        }
    }

    snprintf(extra, sizeof extra, "Content-Length: %zu\r\n", f->len);
    n = netgate_response_head(f->head, tls, body_headers, extra, out, HEAD_MAX + 512);
    if (n >= 0 && send_all(fd, out, n) == 0)
        send_all(fd, (const char *)f->body, f->len);

done:
    free(made);
    free(out);
}

/* Pictures made ahead *********************************************************/

int netgate_prefetch_lines(char *body, const char **referer, const char **accept,
                           struct netgate_prefetch *lines, int most)
{
    char *line = body, *next;
    int n = 0;

    *referer = *accept = "";

    for (; line && *line; line = next)
    {
        char *tab, *cookie;
        size_t len;

        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        len = strlen(line);
        if (len && line[len - 1] == '\r')
            line[--len] = 0;

        if (strncasecmp(line, "Referer:", 8) == 0)
        {
            for (line += 8; *line == ' '; line++)
                ;
            *referer = line;
            continue;
        }
        if (strncasecmp(line, "Accept:", 7) == 0)
        {
            for (line += 7; *line == ' '; line++)
                ;
            *accept = line;
            continue;
        }

        tab = strchr(line, '\t');
        if (!tab || tab == line || n == most)
            continue;
        *tab++ = 0;
        cookie = strchr(tab, '\t');
        if (cookie)
            *cookie++ = 0;

        lines[n].url = line;
        lines[n].ask = tab;
        lines[n].cookie = cookie ? cookie : "";
        n++;
    }

    return n;
}

/* As many at a time as webgate makes */
#define WORKERS (4)

/* The most named at once, which is webgate's too */
#define PREFETCH_MOST (100)

struct job
{
    struct job *next;
    struct netgate_target t;
    char key[NETGATE_TARGET_MAX + 300];
    char *request;
    size_t len;
};

static struct job *queue;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static int workers;

static void job_free(struct job *j)
{
    free(j->request);
    free(j);
}

/* Fetches what is named, the first first, into the cache */
static void *worker(void *arg)
{
    (void)arg;

    for (;;)
    {
        struct job *j;
        char why[512];

        pthread_mutex_lock(&queue_lock);
        while (!queue)
            pthread_cond_wait(&queue_cond, &queue_lock);
        j = queue;
        queue = j->next;
        pthread_mutex_unlock(&queue_lock);

        if (netcache_claim(j->key))
        {
            double t0 = seconds();
            struct fetched *f = fetch_whole(&j->t, j->request, j->len, why, sizeof why);

            trace("made ahead %s: %s in %.1f s", j->key,
                  f ? (netgate_status(f->head) == 200 ? "fetched" : "refused") : why,
                  seconds() - t0);
            netcache_done(j->key, f && netgate_status(f->head) == 200 ? f : NULL);
            if (f)
                fetched_release(f);
        }

        job_free(j);
    }

    return NULL;
}

/* A list in place of the one before, of which what has not begun is
 * dropped: the Atari has moved on to another page, or another part of one */
static void queue_jobs(struct job *jobs)
{
    struct job *old;

    pthread_mutex_lock(&queue_lock);

    old = queue;
    queue = jobs;

    while (workers < WORKERS)
    {
        pthread_attr_t attr;
        pthread_t thread;

        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&thread, &attr, worker, NULL) != 0)
            break;
        pthread_attr_destroy(&attr);
        workers++;
    }

    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&queue_lock);

    while (old)
    {
        struct job *j = old;

        old = j->next;
        job_free(j);
    }
}

/* The request a picture named ahead is fetched with: the page's, as EWeb
 * would ask for it */
static char *prefetch_request(const struct netgate_target *t, const char *agent,
                              const char *accept, const char *referer, const char *cookie,
                              size_t *len)
{
    size_t size = strlen(t->target) + strlen(t->host) + strlen(agent) + strlen(accept)
                  + strlen(referer) + strlen(cookie) + 256;
    char *r = malloc(size), host[300];
    int n;

    if (!r)
        return NULL;

    if (t->port == (t->tls ? 443u : 80u))
        snprintf(host, sizeof host, "%s", t->host);
    else
        snprintf(host, sizeof host, "%s:%u", t->host, t->port);

    n = snprintf(r, size, "GET %s HTTP/1.1\r\nHost: %s\r\n%s%s%s%s%s%s%s%s%s"
                          "Accept-Encoding: identity\r\nConnection: close\r\n\r\n",
                 t->target, host,
                 *agent ? "User-Agent: " : "", agent, *agent ? "\r\n" : "",
                 *accept ? "Accept: " : "", accept, *accept ? "\r\n" : "",
                 *referer ? "Referer: " : "", referer, *referer ? "\r\n" : "");
    if (*cookie && n > 0 && (size_t)n < size)
    {
        /* In ahead of the blank line that ends it */
        n -= 2;
        n += snprintf(r + n, size - n, "Cookie: %s\r\n\r\n", cookie);
    }

    *len = n > 0 && (size_t)n < size ? (size_t)n : 0;
    return r;
}

/*
 * POST http://webgate/prefetch: a page's pictures, a line each, the address
 * and its X-Gateway-Picture and cookies between tabs, after the page's
 * Referer and Accept. Fetched four at a time and kept, so that each asked for
 * after is there at once - see netcache.h. Pens in colours not sent yet are
 * a 409 for the whole list, which EWeb sends them on and names it again.
 */
static void prefetch(int fd, const char *head, const char *left, size_t nleft)
{
    struct netgate_prefetch lines[PREFETCH_MOST];
    struct job *jobs = NULL, **tail = &jobs;
    const char *referer, *accept;
    char agent[256] = "", extra[48];
    long len = netgate_content_length(head);
    uint8_t *body = request_body(fd, left, nleft, len, 1024 * 1024);
    char *text;
    int n, i, made = 0;

    if (!body || !(text = realloc(body, len + 1)))
    {
        free(body);
        answer(fd, "400 Bad Request", "text/plain",
               "webgate: the pictures to make ahead are a POST of lines\n");
        return;
    }
    text[len] = 0;

    netgate_header(head, "User-Agent", agent, sizeof agent);
    n = netgate_prefetch_lines(text, &referer, &accept, lines, PREFETCH_MOST);

    for (i = 0; i < n; i++)
    {
        char line[NETGATE_TARGET_MAX + 300];
        struct netpic_ask ask;
        struct job *j;

        if (netpic_parse_ask(lines[i].ask, &ask) == 0 && ask.pens
            && !netpic_palette_known(ask.pal))
        {
            while (jobs)
            {
                j = jobs;
                jobs = j->next;
                job_free(j);
            }
            free(text);
            answer_with(fd, "409 Conflict", "text/plain", "X-Gateway-Palette: need\r\n",
                        "webgate: send the colours first\n");
            return;
        }

        if (!(j = calloc(1, sizeof *j)))
            break;
        snprintf(line, sizeof line, "GET %s HTTP/1.1", lines[i].url);
        if (netgate_parse_request_line(line, &j->t) != NETGATE_PROXY
            || strcasecmp(j->t.host, "webgate") == 0
            || !(j->request = prefetch_request(&j->t, agent, accept, referer, lines[i].cookie,
                                               &j->len))
            || j->len == 0)
        {
            job_free(j);
            continue;
        }
        cache_key(&j->t, j->key, sizeof j->key);

        *tail = j;
        tail = &j->next;
        made++;
    }

    free(text);
    queue_jobs(jobs);
    trace("%d pictures named ahead", made);

    snprintf(extra, sizeof extra, "X-Gateway-Prefetch: %d\r\n", made);
    answer_with(fd, "204 No Content", "text/plain", extra, "");
}

/*
 * http://webgate/... - the gateway's own: the colours pens are made in, and
 * the pictures to make ahead.
 */
static void own(int fd, const struct netgate_target *t, const char *head,
                const char *left, size_t nleft)
{
    if (strcmp(t->method, "POST") == 0 && strcmp(t->target, "/palette") == 0)
    {
        long len = netgate_content_length(head);
        uint8_t *rgb = request_body(fd, left, nleft, len, 256 * 3);
        uint32_t crc = rgb ? netpic_palette_add(rgb, len) : 0;
        char extra[48];

        free(rgb);
        if (!crc)
        {
            answer(fd, "400 Bad Request", "text/plain", "webgate: 2 to 256 colours, as R G B bytes\n");
            return;
        }
        snprintf(extra, sizeof extra, "X-Gateway-Palette: %08x\r\n", crc);
        answer_with(fd, "204 No Content", "text/plain", extra, "");
        return;
    }

    if (strcmp(t->method, "POST") == 0 && strcmp(t->target, "/prefetch") == 0)
    {
        prefetch(fd, head, left, nleft);
        return;
    }

    answer(fd, "200 OK", "text/plain",
           "webgate (tosemu) is running. Put this address in EWeb's gateway setting.\n");
}

/*
 * A picture asked for with X-Gateway-Picture: from what is kept of it, or
 * what is being fetched of it already, or fetched now and kept. A request
 * that asks whether what the Atari has is still good is the server's to
 * answer, and goes to it every time.
 */
static void picture(int fd, const struct netgate_target *t, const char *head,
                    const struct netpic_ask *ask)
{
    char key[NETGATE_TARGET_MAX + 300], value[8], why[512], *out = malloc(HEAD_MAX + 512);
    int asking = netgate_header(head, "If-None-Match", value, sizeof value)
                 || netgate_header(head, "If-Modified-Since", value, sizeof value);
    int claimed = 0;
    struct fetched *f = NULL;
    double t0 = seconds();
    const char *how = "kept";
    char said[192] = "";
    long n;

    if (!out)
        return;

    cache_key(t, key, sizeof key);

    if (!asking)
    {
        f = netcache_get(key, 0);
        if (!f)
        {
            how = "waited for";
            f = netcache_get(key, IO_SECONDS * 1000L);
        }
        if (!f && !(claimed = netcache_claim(key)))
            f = netcache_get(key, IO_SECONDS * 1000L);
    }

    if (!f)
    {
        how = asking ? "asked of the server" : "fetched";
        n = netgate_request_head(head, t, out, HEAD_MAX + 512);
        f = n < 0 ? NULL : fetch_whole(t, out, n, why, sizeof why);
        if (n < 0)
            snprintf(why, sizeof why, "the request could not be sent");
        if (claimed)
            netcache_done(key, f && netgate_status(f->head) == 200 ? f : NULL);
    }

    if (f)
    {
        double t1 = seconds();

        send_fetched(fd, f, ask, said, sizeof said);
        fetched_release(f);
        trace("picture %s: %s in %.1f s, made and sent in %.2f s: %s", key, how,
              t1 - t0, seconds() - t1, said);
    }
    else
    {
        trace("picture %s: %s", key, why);
        refuse(fd, "502 Bad Gateway", why);
    }

    free(out);
}

/* What a thread is handed */
struct serving
{
    int fd;
    long budget;
};

static void *serve(void *arg)
{
    struct serving *serving = arg;
    int fd = serving->fd;
    long budget = serving->budget;
    double started = seconds();
    char *head = malloc(HEAD_MAX), *out = malloc(HEAD_MAX + 512), *end;
    struct netgate_target *t = malloc(sizeof *t);
    struct upstream u = { -1 };
    struct netpic_ask ask;
    char why[512], tls[128] = "", value[256];
    ssize_t got;
    long len, n;
    int kind, pictured;

    if (!head || !out || !t)
        goto done;

    timeouts(fd);

    got = read_head(fd, NULL, head, HEAD_MAX, &end);
    if (got < 0)
        goto done;

    kind = netgate_parse_request_line(head, t);
    if (kind == NETGATE_BAD)
    {
        refuse(fd, "400 Bad Request", "that is not a request it can read");
        goto done;
    }

    if (kind == NETGATE_ORIGIN || strcasecmp(t->host, "webgate") == 0)
    {
        own(fd, t, head, end, got - (end - head));
        goto done;
    }

    pictured = strcmp(t->method, "GET") == 0
               && netgate_header(head, "X-Gateway-Picture", value, sizeof value)
               && netpic_parse_ask(value, &ask) == 0;
    ask.budget = budget;

    /* Pens in colours it was never sent: asked for first, before the
     * server is, which is what EWeb sends them on */
    if (pictured && ask.pens && !netpic_palette_known(ask.pal))
    {
        answer_with(fd, "409 Conflict", "text/plain", "X-Gateway-Palette: need\r\n",
                    "webgate: send the colours first\n");
        goto done;
    }

    if (pictured)
    {
        picture(fd, t, head, &ask);
        goto done;
    }

    len = netgate_content_length(head);
    if (len == -2)
    {
        refuse(fd, "501 Not Implemented", "a request body without a length");
        goto done;
    }

#ifndef HAVE_OPENSSL
    if (t->tls)
    {
        refuse(fd, "502 Bad Gateway", "this tosemu was built without OpenSSL, so it has no TLS");
        goto done;
    }
#endif

    u.fd = connect_upstream(t->host, t->port, why, sizeof why);
    if (u.fd < 0)
    {
        refuse(fd, "502 Bad Gateway", why);
        goto done;
    }

#ifdef HAVE_OPENSSL
    if (t->tls && tls_start(&u, t->host, tls, sizeof tls, why, sizeof why) < 0)
    {
        refuse(fd, "502 Bad Gateway", why);
        goto done;
    }
#endif

    n = netgate_request_head(head, t, out, HEAD_MAX + 512);
    if (n < 0 || up_write(&u, out, n) < 0)
    {
        refuse(fd, "502 Bad Gateway", "the request could not be sent");
        goto done;
    }

    /* The body: whatever came with the head, then the rest of its length */
    if (len > 0)
    {
        long have = got - (end - head);

        if (have > len)
            have = len;
        if (have > 0 && up_write(&u, end, have) < 0)
            goto done;
        len -= have;

        while (len > 0)
        {
            ssize_t r = recv(fd, out, len < HEAD_MAX ? len : HEAD_MAX, 0);

            if (r <= 0 || up_write(&u, out, r) < 0)
                goto done;
            len -= r;
        }
    }

    got = read_head(-1, &u, head, HEAD_MAX, &end);
    if (got < 0)
    {
        refuse(fd, "502 Bad Gateway", "no answer from the server");
        goto done;
    }

    n = netgate_response_head(head, t->tls ? tls : NULL, NULL, NULL, out, HEAD_MAX + 512);
    if (n < 0)
    {
        refuse(fd, "502 Bad Gateway", "an answer with a head too large");
        goto done;
    }
    if (send_all(fd, out, n) < 0
        || (got > end - head && send_all(fd, end, got - (end - head)) < 0))
        goto done;

    for (;;)
    {
        ssize_t r = up_read(&u, out, HEAD_MAX);

        if (r <= 0 || send_all(fd, out, r) < 0)
            break;
    }

    trace("%s %s://%s%s: %d in %.1f s", t->method, t->tls ? "https" : "http", t->host,
          t->target, netgate_status(head), seconds() - started);

done:
#ifdef HAVE_OPENSSL
    if (u.ssl)
        SSL_free(u.ssl);
#endif
    if (u.fd >= 0)
        close(u.fd);
    close(fd);
    free(head);
    free(out);
    free(t);
    free(serving);
    return NULL;
}

int netgate_serve(int fd, long budget)
{
    struct serving *s = malloc(sizeof *s);
    pthread_attr_t attr;
    pthread_t thread;
    int r;

    if (!s)
        return -1;
    s->fd = fd;
    s->budget = budget;
    tracing = setting_flag("TOSEMU_TRACE_GATEWAY");

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    r = pthread_create(&thread, &attr, serve, s);
    pthread_attr_destroy(&attr);

    if (r != 0)
        free(s);
    return r == 0 ? 0 : -1;
}
