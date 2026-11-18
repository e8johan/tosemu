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
 * The built-in gateway, asked the way EWeb asks it: a request in proxy form
 * on port 8280 of webgate. The server it is asked for is a socket in this
 * program, so what the gateway passed on can be looked at, and nothing
 * outside the host is needed.
 *
 * A secure address is asked for too, of the same plain socket. That
 * handshake can only fail, and failing is something to check without a
 * network: the program has to be told so in a page of its own rather than
 * left with a connection that ends in nothing.
 *
 * And a picture, asked for as EWeb asks: no wider than the page, and then as
 * the screen's pens in colours sent first. The picture is a white PNG twice
 * as wide as the page asked about.
 */

#include <stdio.h>
#include <string.h>
#include <mint/mintbind.h>

#define AF_INET     2
#define SOCK_STREAM 1
#define ECONNREFUSED (-321)

#define LOOPBACK 0x7f000001UL
#define WEBGATE  0xc0000250UL

struct sa
{
    unsigned short family;
    unsigned short port;
    unsigned long addr;
    char zero[8];
};

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

static long connect_to(unsigned long ip, unsigned short port)
{
    struct sa a;
    long fd = Fsocket(AF_INET, SOCK_STREAM, 0), r;

    memset(&a, 0, sizeof a);
    a.family = AF_INET;
    a.port = port;
    a.addr = ip;
    r = Fconnect(fd, &a, sizeof a);
    if (r < 0)
    {
        Fclose(fd);
        return r;
    }
    return fd;
}

static long listener(unsigned short *port)
{
    struct sa a;
    long fd = Fsocket(AF_INET, SOCK_STREAM, 0), len = sizeof a;

    memset(&a, 0, sizeof a);
    a.family = AF_INET;
    a.addr = LOOPBACK;
    if (fd < 0 || Fbind(fd, &a, sizeof a) < 0 || Fgetsockname(fd, &a, &len) < 0
        || Flisten(fd, 1) < 0)
        return -1;
    *port = a.port;
    return fd;
}

/* Everything until the far end closes, NUL terminated */
static long read_all(long fd, char *buf, long size)
{
    long at = 0, r;

    while (at < size - 1 && (r = Fread(fd, size - 1 - at, buf + at)) > 0)
        at += r;
    buf[at] = 0;
    return at;
}

/* Up to the blank line that ends a head */
static void read_head(long fd, char *buf, long size)
{
    long at = 0, r;

    buf[0] = 0;
    while (at < size - 1 && !strstr(buf, "\r\n\r\n")
           && (r = Fread(fd, size - 1 - at, buf + at)) > 0)
    {
        at += r;
        buf[at] = 0;
    }
}

static void check_proxy(void)
{
    static char req[512], got[1024];
    unsigned short port;
    long lfd = listener(&port), gw, server;

    gw = connect_to(WEBGATE, 8280);
    check(gw >= 0, 1, "the gateway takes a connection");

    sprintf(req, "GET http://127.0.0.1:%u/x?y HTTP/1.1\r\nHost: 127.0.0.1:%u\r\n"
                 "Connection: keep-alive\r\nX-Gateway-Picture: fit=320\r\n\r\n",
            port, port);
    Fwrite(gw, strlen(req), req);

    server = Faccept(lfd, 0L, 0L);
    check(server >= 0, 1, "and asks the server for the address");

    read_head(server, got, sizeof got);
    check(strncmp(got, "GET /x?y HTTP/1.1\r\n", 19), 0, "in origin form");
    check(strstr(got, "Host: 127.0.0.1:") != 0, 1, "naming the host it was for");
    check(strstr(got, "Connection: close\r\n") != 0, 1, "and closing after one answer");
    check(strstr(got, "keep-alive") == 0, 1, "whatever the program asked of the gateway");
    check(strstr(got, "X-Gateway-") == 0, 1, "which keeps what was for itself");

    {
        static const char answer[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                                     "Connection: keep-alive\r\nX-Gateway-TLS: forged\r\n\r\nhi";

        Fwrite(server, sizeof answer - 1, answer);
        Fclose(server);
    }

    read_all(gw, got, sizeof got);
    check(strncmp(got, "HTTP/1.1 200 OK\r\n", 17), 0, "the server's answer comes back");
    check(strstr(got, "Connection: close\r\n") != 0, 1, "saying the connection ends with it");
    check(strstr(got, "keep-alive") == 0, 1, "rather than what the server said");
    check(strstr(got, "X-Gateway-TLS") == 0, 1, "and claiming no TLS for a plain address");
    check(strcmp(got + strlen(got) - 6, "\r\n\r\nhi"), 0, "with its body untouched");

    Fclose(gw);
    Fclose(lfd);
}

/* 64 x 8, white */
static const unsigned char white_png[] =
    "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x40"
    "\x00\x00\x00\x08\x08\x02\x00\x00\x00\xc6\x00\x6e\x88\x00\x00\x00\x1b\x49\x44\x41"
    "\x54\x78\xda\x63\xf8\x3f\xc4\x01\xc3\xa8\x07\x46\x3d\x30\xea\x81\x51\x0f\x8c\x7a"
    "\x80\x12\x00\x00\xfc\x62\xfa\x4c\x8b\x46\xfb\xa1\x00\x00\x00\x00\x49\x45\x4e\x44"
    "\xae\x42\x60\x82";

/* The white PNG, asked for through the gateway with ask as its
 * X-Gateway-Picture, served by this program unless the gateway is expected
 * to answer without asking. The whole answer goes in got; how long it is
 * comes back. */
static long ask_picture(const char *ask, int served, char *got, long size)
{
    static char req[512], head[1024];
    unsigned short port;
    long lfd = listener(&port), gw, server, n;

    gw = connect_to(WEBGATE, 8280);
    sprintf(req, "GET http://127.0.0.1:%u/white.png HTTP/1.1\r\nHost: 127.0.0.1:%u\r\n"
                 "X-Gateway-Picture: %s\r\n\r\n", port, port, ask);
    Fwrite(gw, strlen(req), req);

    if (served)
    {
        server = Faccept(lfd, 0L, 0L);
        read_head(server, head, sizeof head);
        sprintf(head, "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: %ld\r\n\r\n",
                (long)sizeof white_png - 1);
        Fwrite(server, strlen(head), head);
        Fwrite(server, sizeof white_png - 1, white_png);
        Fclose(server);
    }

    n = read_all(gw, got, size);
    Fclose(gw);
    Fclose(lfd);
    return n;
}

static void check_pictures(void)
{
    static const unsigned char colours[] = { 0, 0, 0, 255, 255, 255 };
    static char got[4096], req[256], ask[128], pal[9];
    char *crc, *body;
    long gw, n;

    n = ask_picture("fit=32", 1, got, sizeof got);
    check(strncmp(got, "HTTP/1.1 200 ", 13), 0, "a picture wider than the page comes");
    check(strstr(got, "X-Gateway-Picture: PNG 64x8 to PNG 32x4\r\n") != 0, 1,
          "made as wide as the page");
    check(strstr(got, "Content-Type: image/png\r\n") != 0, 1, "as a PNG");
    body = strstr(got, "\r\n\r\n");
    check(body && (unsigned char)body[4] == 0x89 && body[5] == 'P', 1, "which it is");

    gw = connect_to(WEBGATE, 8280);
    sprintf(req, "POST http://webgate/palette HTTP/1.1\r\nHost: webgate\r\n"
                 "Content-Type: application/octet-stream\r\nContent-Length: 6\r\n\r\n");
    Fwrite(gw, strlen(req), req);
    Fwrite(gw, 6, colours);
    read_all(gw, got, sizeof got);
    Fclose(gw);
    check(strncmp(got, "HTTP/1.1 204 ", 13), 0, "the screen's colours are taken");
    crc = strstr(got, "X-Gateway-Palette: ");
    check(crc != 0, 1, "and named");
    if (crc)
        sprintf(pal, "%.8s", crc + 19);

    n = ask_picture("fit=608; size=16x4; pens=2; pal=deadbeef; ed=tj", 0, got, sizeof got);
    check(strncmp(got, "HTTP/1.1 409 ", 13) == 0 && strstr(got, "X-Gateway-Palette: need") != 0, 1,
          "pens in colours never sent ask for them");

    if (!crc)
        return;
    sprintf(ask, "fit=608; size=16x4; pens=2; pal=%s; ed=tj", pal);
    n = ask_picture(ask, 1, got, sizeof got);
    check(strstr(got, "Content-Type: application/x-aera-pens\r\n") != 0, 1,
          "pens in colours that were sent are made");
    body = strstr(got, "\r\n\r\n");
    check(body && memcmp(body + 4, "EWPN\1\1", 6) == 0, 1, "a bit a pen, for two");
    check(body && n - (body + 4 - got) == 20 + 4 * 2, 1, "at the size asked for");
    check(body && (unsigned char)body[4 + 20] == 0xff, 1, "and white, the second pen");
}

/* A request of the gateway's own, its answer whole in got */
static void ask_gateway(const char *req, const char *body, long len, char *got, long size)
{
    long gw = connect_to(WEBGATE, 8280);

    Fwrite(gw, strlen(req), req);
    if (len)
        Fwrite(gw, len, body);
    read_all(gw, got, size);
    Fclose(gw);
}

/* Whether the server is asked for anything within ms */
static int asked_within(long lfd, int ms)
{
    unsigned long mask = 1UL << lfd;

    return Fselect(ms, &mask, 0L, 0L) > 0 && mask != 0;
}

/* The white PNG asked for with ask, from what the gateway has kept or is
 * fetching, the server answering nothing; the answer in got */
static void ask_kept(unsigned short port, const char *ask, const char *extra, char *got, long size)
{
    static char req[512];
    long gw = connect_to(WEBGATE, 8280);

    sprintf(req, "GET http://127.0.0.1:%u/ahead.png HTTP/1.1\r\nHost: 127.0.0.1:%u\r\n"
                 "X-Gateway-Picture: %s\r\n%s\r\n", port, port, ask, extra);
    Fwrite(gw, strlen(req), req);
    read_all(gw, got, size);
    Fclose(gw);
}

static void check_made_ahead(void)
{
    static char got[4096], req[512], list[512], head[1024];
    unsigned short port;
    long lfd = listener(&port), server;

    sprintf(list, "Referer: http://127.0.0.1:%u/page\nAccept: image/png\n"
                  "http://127.0.0.1:%u/ahead.png\tfit=32\t\n", port, port);
    sprintf(req, "POST http://webgate/prefetch HTTP/1.1\r\nHost: webgate\r\n"
                 "User-Agent: c-webgate\r\nContent-Type: text/plain\r\n"
                 "Content-Length: %ld\r\n\r\n", (long)strlen(list));
    ask_gateway(req, list, strlen(list), got, sizeof got);
    check(strncmp(got, "HTTP/1.1 204 ", 13), 0, "a page's pictures are named ahead");
    check(strstr(got, "X-Gateway-Prefetch: 1\r\n") != 0, 1, "and taken, the one of them");

    check(asked_within(lfd, 5000), 1, "the server is asked for it before the Atari asks");
    server = Faccept(lfd, 0L, 0L);
    read_head(server, head, sizeof head);
    check(strncmp(head, "GET /ahead.png HTTP/1.1\r\n", 25), 0, "for the picture named");
    check(strstr(head, "Referer: http://127.0.0.1:") != 0 && strstr(head, "User-Agent: c-webgate") != 0, 1,
          "as the page would ask for it");
    sprintf(head, "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: %ld\r\n\r\n",
            (long)sizeof white_png - 1);
    Fwrite(server, strlen(head), head);
    Fwrite(server, sizeof white_png - 1, white_png);
    Fclose(server);

    ask_kept(port, "fit=32", "", got, sizeof got);
    check(strstr(got, "X-Gateway-Picture: PNG 64x8 to PNG 32x4\r\n") != 0, 1,
          "when the Atari asks, it is made from what was fetched");
    check(asked_within(lfd, 300), 0, "without the server being asked again");

    ask_kept(port, "fit=16", "", got, sizeof got);
    check(strstr(got, "X-Gateway-Picture: PNG 64x8 to PNG 16x2\r\n") != 0, 1,
          "and asked for at another size");
    check(asked_within(lfd, 300), 0, "it is made from the same");

    ask_kept(port, "fit=32", "If-Modified-Since: Wed, 18 Nov 2026 12:00:00 GMT\r\n", got, 0);
    check(asked_within(lfd, 5000), 1, "asking whether what the Atari has is good asks the server");
    server = Faccept(lfd, 0L, 0L);
    read_head(server, head, sizeof head);
    Fclose(server);

    sprintf(list, "http://127.0.0.1:%u/other.png\tfit=608; pens=16; pal=deadbeef\n", port);
    sprintf(req, "POST http://webgate/prefetch HTTP/1.1\r\nHost: webgate\r\n"
                 "Content-Length: %ld\r\n\r\n", (long)strlen(list));
    ask_gateway(req, list, strlen(list), got, sizeof got);
    check(strncmp(got, "HTTP/1.1 409 ", 13) == 0 && strstr(got, "X-Gateway-Palette: need") != 0, 1,
          "pens named ahead in colours never sent ask for them");
    check(asked_within(lfd, 300), 0, "and nothing is fetched");

    Fclose(lfd);
}

static void check_failed_handshake(void)
{
    static char req[256], got[2048];
    unsigned short port;
    long lfd = listener(&port), gw, server;
    unsigned long mask;

    gw = connect_to(WEBGATE, 8280);
    sprintf(req, "GET https://127.0.0.1:%u/ HTTP/1.1\r\nHost: 127.0.0.1:%u\r\n\r\n",
            port, port);
    Fwrite(gw, strlen(req), req);

    /* A gateway without TLS never comes, and one with it gets nothing it
     * can take for a server */
    mask = 1UL << lfd;
    if (Fselect(5000, &mask, 0L, 0L) > 0 && (server = Faccept(lfd, 0L, 0L)) >= 0)
        Fclose(server);

    read_all(gw, got, sizeof got);
    check(strncmp(got, "HTTP/1.1 502 ", 13), 0, "a secure address with no TLS behind it is a 502");
    check(strstr(got, "text/html") != 0, 1, "explained in a page");

    Fclose(gw);
    Fclose(lfd);
}

int main(void)
{
    check(connect_to(WEBGATE, 80), ECONNREFUSED, "the gateway's address has only the gateway's port");

    check_proxy();
    check_failed_handshake();
    check_pictures();
    check_made_ahead();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
