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
 * MiNTnet's socket calls, both ends in this one program and over the host's
 * loopback, so nothing outside the machine running the suite is needed.
 *
 * The client end connects the way EWeb does: without blocking, then Fselect
 * for writability and SO_ERROR for how it went, which is the path that has
 * to keep a GEM program's window alive while a far server makes up its mind.
 */

#include <stdio.h>
#include <string.h>
#include <mint/mintbind.h>

#define AF_INET     2
#define AF_UNIX     1
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOL_SOCKET  0xffff
#define SO_ERROR    4
#define F_GETFL     3
#define F_SETFL     4
#define O_NDELAY    0x100
#define FIONREAD    (('F' << 8) | 1)

#define EINVFN        (-32)
#define EAFNOSUPPORT  (-309)
#define ECONNREFUSED  (-321)
#define EINPROGRESS   (-325)
#define EAGAIN        (-326)

#define LOOPBACK 0x7f000001UL

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

static void loopback(struct sa *a, unsigned short port)
{
    memset(a, 0, sizeof *a);
    a->family = AF_INET;
    a->port = port;
    a->addr = LOOPBACK;
}

/* A socket bound to the loopback on a port the host picked, and that port */
static long bound(long type, unsigned short *port)
{
    struct sa a;
    long fd = Fsocket(AF_INET, type, 0), len = sizeof a;

    loopback(&a, 0);
    if (fd < 0 || Fbind(fd, &a, sizeof a) < 0 || Fgetsockname(fd, &a, &len) < 0)
        return -1;
    *port = a.port;
    return fd;
}

static void check_stream(void)
{
    struct sa a;
    unsigned short port;
    long listener, client, server, r, flags, len, err;
    unsigned long mask;
    char buf[16];

    listener = bound(SOCK_STREAM, &port);
    check(listener >= 0, 1, "a socket bound to the loopback");
    check(port != 0, 1, "on a port of the host's choosing");
    check(Flisten(listener, 1), 0, "listening");

    client = Fsocket(AF_INET, SOCK_STREAM, 0);
    flags = Fcntl(client, 0, F_GETFL);
    check(flags >= 0 && !(flags & O_NDELAY), 1, "a socket blocks to start with");
    check(Fcntl(client, flags | O_NDELAY, F_SETFL), 0, "and can be told not to");

    loopback(&a, port);
    r = Fconnect(client, &a, sizeof a);
    check(r == 0 || r == EINPROGRESS, 1, "so a connection is under way rather than waited for");

    mask = 1UL << client;
    check(Fselect(2000, 0L, &mask, 0L), 1, "until it is writable");
    check(mask, 1UL << client, "which is this socket");

    err = -1;
    len = sizeof err;
    check(Fgetsockopt(client, SOL_SOCKET, SO_ERROR, &err, &len), 0, "and SO_ERROR says how it went");
    check(err, 0, "which is well");

    len = sizeof a;
    memset(&a, 0, sizeof a);
    server = Faccept(listener, &a, &len);
    check(server >= 0, 1, "the other end is accepted");
    check(a.family == AF_INET && a.addr == LOOPBACK, 1, "from the loopback");

    check(Fread(client, sizeof buf, buf), EAGAIN, "nothing to read yet, and no waiting for it");

    check(Fwrite(client, 5, "hello"), 5, "one end writes");
    memset(buf, 0, sizeof buf);
    check(Fread(server, 5, buf), 5, "the other reads");
    check(strcmp(buf, "hello"), 0, "what was written");

    check(Fwrite(server, 6, "world!"), 6, "and back");
    mask = 1UL << client;
    check(Fselect(2000, &mask, 0L, 0L), 1, "the answer makes it readable");
    len = 0;
    check(Fcntl(client, &len, FIONREAD), 0, "FIONREAD");
    check(len, 6, "says how much there is");
    memset(buf, 0, sizeof buf);
    check(Fread(client, sizeof buf, buf), 6, "and that much is read");
    check(strcmp(buf, "world!"), 0, "the answer");

    len = sizeof a;
    check(Fgetpeername(client, &a, &len), 0, "the far end has a name");
    check(a.port, port, "which is where it was connected to");

    check(Fclose(server), 0, "the far end closes");
    mask = 1UL << client;
    check(Fselect(2000, &mask, 0L, 0L), 1, "which is something to read");
    check(Fread(client, sizeof buf, buf), 0, "the end of it");

    Fclose(client);
    Fclose(listener);

    check(Fcntl(1, 0, F_GETFL), EINVFN, "a handle that is not a socket has no Fcntl, as before");
}

static void check_refused(void)
{
    struct sa a;
    unsigned short port;
    long closed = bound(SOCK_STREAM, &port), fd = Fsocket(AF_INET, SOCK_STREAM, 0);

    loopback(&a, port);
    check(Fconnect(fd, &a, sizeof a), ECONNREFUSED, "a port nobody listens on refuses");
    Fclose(fd);
    Fclose(closed);
}

static void check_datagrams(void)
{
    struct sa a, from;
    unsigned short port;
    long rx = bound(SOCK_DGRAM, &port), tx = Fsocket(AF_INET, SOCK_DGRAM, 0), len;
    char buf[16];

    loopback(&a, port);
    check(Fsendto(tx, "ping", 4L, 0L, &a, (long)sizeof a), 4, "a datagram goes");

    memset(buf, 0, sizeof buf);
    len = sizeof from;
    check(Frecvfrom(rx, buf, (long)sizeof buf, 0L, &from, &len), 4, "and arrives");
    check(strcmp(buf, "ping"), 0, "whole");
    check(from.family == AF_INET && from.addr == LOOPBACK, 1, "saying who sent it");

    Fclose(rx);
    Fclose(tx);
}

int main(void)
{
    check(Fsocket(AF_UNIX, SOCK_STREAM, 0), EAFNOSUPPORT, "only the internet is spoken");

    check_stream();
    check_refused();
    check_datagrams();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
