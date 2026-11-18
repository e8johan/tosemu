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
 * STinG, found the way a program finds it: the STiK cookie, the driver list
 * behind it, and the TRANSPORT_TCPIP table that get_dftab hands out.
 *
 * Its functions are Pure C's, which take their arguments as packed words and
 * longs where GCC would push every short as a long, so each call goes through
 * sting_call below with the arguments laid out by hand - the way EWeb calls
 * them. The far end is a MiNTnet socket in this same program.
 */

#include <stdio.h>
#include <string.h>
#include <mint/mintbind.h>

#define AF_INET     2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define E_NORMAL     0
#define E_NODATA    (-2)
#define E_EOF       (-3)
#define E_REFUSE    (-7)
#define E_BADHANDLE (-9)
#define TESTABLISH   4

#define LOOPBACK  0x7f000001UL
#define WEBGATE   0xc0000250UL

enum {
    KRMALLOC, KRFREE, KRGETFREE, KRREALLOC, GET_ERR_TEXT, GETVSTR,
    CARRIER_DETECT, TCP_OPEN, TCP_CLOSE, TCP_SEND, TCP_WAIT_STATE,
    TCP_ACK_WAIT, UDP_OPEN, UDP_CLOSE, UDP_SEND, CNKICK, CNBYTE_COUNT,
    CNGET_CHAR, CNGET_NDB, CNGET_BLOCK, HOUSEKEEP, RESOLVE
};

typedef struct {
    char magic[10];
    void *get_dftab;
    void *etm_exec;
    void *cfg;
    void *basepage;
} DRV_LIST;

typedef struct {
    const char *module, *author, *version;
    void *fn[32];
} TPL;

struct sa
{
    unsigned short family;
    unsigned short port;
    unsigned long addr;
    char zero[8];
};

long sting_call(void *fn, const short *args, long nwords);

__asm__(
"       .text\n"
"       .even\n"
"       .globl  _sting_call\n"
"_sting_call:\n"
"       movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
"       move.l  48(%sp),%a1\n"
"       move.l  52(%sp),%a0\n"
"       move.l  56(%sp),%d0\n"
"       move.l  %sp,%a5\n"
"       move.l  %d0,%d1\n"
"       add.l   %d1,%d1\n"
"       sub.l   %d1,%sp\n"
"       move.l  %sp,%a2\n"
"       bra.s   2f\n"
"1:     move.w  (%a0)+,(%a2)+\n"
"2:     dbra    %d0,1b\n"
"       jsr     (%a1)\n"
"       move.l  %a5,%sp\n"
"       movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
"       rts\n"
);

static TPL *tpl;

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

static void put_long(short *a, unsigned long v)
{
    a[0] = (short)(v >> 16);
    a[1] = (short)(v & 0xffff);
}

/* Each of these lays its arguments out as the function's prototype has them */
static long call0(int fn)
{
    return sting_call(tpl->fn[fn], 0, 0);
}

static long call_w(int fn, short w)
{
    return sting_call(tpl->fn[fn], &w, 1);
}

static long call_l(int fn, unsigned long l)
{
    short a[2];

    put_long(a, l);
    return sting_call(tpl->fn[fn], a, 2);
}

/* cn, a pointer, a length: TCP_send, UDP_send, CNget_block */
static short call_wlw(int fn, short cn, const void *p, short len)
{
    short a[4];

    a[0] = cn;
    put_long(a + 1, (unsigned long)p);
    a[3] = len;
    return (short)sting_call(tpl->fn[fn], a, 4);
}

static short tcp_open(unsigned long ip, unsigned short port)
{
    short a[5];

    put_long(a, ip);
    a[2] = port;
    a[3] = 0;
    a[4] = 4096;
    return (short)sting_call(tpl->fn[TCP_OPEN], a, 5);
}

static short wait_established(short cn, short seconds)
{
    short a[3] = { cn, TESTABLISH, seconds };

    return (short)sting_call(tpl->fn[TCP_WAIT_STATE], a, 3);
}

static short tcp_close(short cn)
{
    short a[4];

    a[0] = cn;
    a[1] = 0;
    put_long(a + 2, 0);
    return (short)sting_call(tpl->fn[TCP_CLOSE], a, 4);
}

static short resolve(const char *name, unsigned long *ip)
{
    short a[7];

    put_long(a, (unsigned long)name);
    put_long(a + 2, 0);
    put_long(a + 4, (unsigned long)ip);
    a[6] = 1;
    return (short)sting_call(tpl->fn[RESOLVE], a, 7);
}

static short udp_open(unsigned long ip, unsigned short port)
{
    short a[3];

    put_long(a, ip);
    a[2] = port;
    return (short)sting_call(tpl->fn[UDP_OPEN], a, 3);
}

/* Until there are at least want bytes, or something other than nothing */
static short count_until(short cn, short want)
{
    short got = 0;
    long i;

    for (i = 0; i < 200000; i++)
    {
        got = (short)call_w(CNBYTE_COUNT, cn);
        if (got >= want || (got < 0 && got != E_NODATA))
            break;
    }
    return got;
}

static long jar;

static long get_jar(void)
{
    jar = *(long *)0x5a0;
    return 0;
}

static DRV_LIST *find_sting(void)
{
    long *c;

    Supexec(get_jar);
    if (!jar)
        return 0;

    for (c = (long *)jar; c[0]; c += 2)
        if (c[0] == 0x5354694bL)    /* STiK */
            return (DRV_LIST *)c[1];
    return 0;
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

static void check_finding(void)
{
    DRV_LIST *d = find_sting();
    short a[2];

    check(d != 0, 1, "there is a STiK cookie");
    if (!d)
        return;
    check(memcmp(d->magic, "STiKmagic", 10), 0, "pointing at a driver list");

    put_long(a, (unsigned long)"NOTHING");
    check(sting_call(d->get_dftab, a, 2), 0, "which has nothing by another name");

    put_long(a, (unsigned long)"TRANSPORT_TCPIP");
    tpl = (TPL *)sting_call(d->get_dftab, a, 2);
    check(tpl != 0, 1, "and has the transport layer");
    if (tpl)
        check(strcmp(tpl->module, "TRANSPORT_TCPIP"), 0, "which says what it is");
}

static void check_small_things(void)
{
    const char *s;
    long block;

    check(call0(CARRIER_DETECT), 1, "there is a carrier");

    s = (const char *)call_l(GETVSTR, (unsigned long)"NAMESERVER");
    check(s && strcmp(s, "0") == 0, 1, "no nameserver of its own to ask");

    s = (const char *)call_w(GET_ERR_TEXT, E_REFUSE);
    check(s && strcmp(s, "Connection refused by remote host") == 0, 1, "an error has words");

    block = call_l(KRMALLOC, 100);
    check(block != 0, 1, "KRmalloc");
    check(call_w(KRGETFREE, 1) > 0, 1, "with room left after it");
    call_l(KRFREE, block);
}

static void check_names(void)
{
    unsigned long ip = 0;

    check(resolve("localhost", &ip), 1, "a name is found");
    check(ip, LOOPBACK, "where the host has it");
    check(resolve("webgate", &ip), 1, "and the gateway's");
    check(ip, WEBGATE, "where the emulator answers it");
    check(resolve("no.such.name.invalid", &ip) < 0, 1, "and a name that is not, is not");
}

static void check_connection(void)
{
    unsigned short port;
    long lfd = listener(&port), server;
    short cn;
    char buf[16];

    cn = tcp_open(LOOPBACK, port);
    check(cn > 0, 1, "TCP_open");
    check(wait_established(cn, 2), E_NORMAL, "and the connection is made");

    server = Faccept(lfd, 0L, 0L);
    check(server >= 0, 1, "at the other end too");

    check(call_wlw(TCP_SEND, cn, "hello", 5), E_NORMAL, "TCP_send");
    memset(buf, 0, sizeof buf);
    check(Fread(server, 5, buf), 5, "arrives");
    check(strcmp(buf, "hello"), 0, "as sent");

    Fwrite(server, 6, "world!");
    check(count_until(cn, 6), 6, "CNbyte_count sees the answer");
    check(call_wlw(CNGET_BLOCK, cn, buf, 7), E_NODATA, "CNget_block takes all it asks for or nothing");
    memset(buf, 0, sizeof buf);
    check(call_wlw(CNGET_BLOCK, cn, buf, 6), 6, "and all of it");
    check(strcmp(buf, "world!"), 0, "is the answer");

    Fclose(server);
    check(count_until(cn, 1), E_EOF, "the far end closing is the end");

    check(tcp_close(cn), E_NORMAL, "TCP_close");
    check(call_w(CNBYTE_COUNT, cn), E_BADHANDLE, "after which the handle is gone");

    /* The listener is not listening any more */
    Fclose(lfd);
    cn = tcp_open(LOOPBACK, port);
    check(cn == E_REFUSE || (cn > 0 && wait_established(cn, 2) == E_REFUSE), 1,
          "a port nobody listens on refuses");
    if (cn > 0)
        tcp_close(cn);
}

static void check_datagrams(void)
{
    struct sa a, from;
    long fd = Fsocket(AF_INET, SOCK_DGRAM, 0), len = sizeof a, got;
    short cn;
    char buf[16];

    memset(&a, 0, sizeof a);
    a.family = AF_INET;
    a.addr = LOOPBACK;
    Fbind(fd, &a, sizeof a);
    Fgetsockname(fd, &a, &len);

    cn = udp_open(LOOPBACK, a.port);
    check(cn > 0, 1, "UDP_open");
    check(call_wlw(UDP_SEND, cn, "ping", 4), E_NORMAL, "UDP_send");

    memset(buf, 0, sizeof buf);
    len = sizeof from;
    got = Frecvfrom(fd, buf, (long)sizeof buf, 0L, &from, &len);
    check(got == 4 && strcmp(buf, "ping") == 0, 1, "arrives");

    Fsendto(fd, "pong", 4L, 0L, &from, (long)sizeof from);
    check(count_until(cn, 4), 4, "the answer is counted");
    memset(buf, 0, sizeof buf);
    check(call_wlw(CNGET_BLOCK, cn, buf, 4), 4, "and read");
    check(strcmp(buf, "pong"), 0, "whole");

    check(call_w(UDP_CLOSE, cn), E_NORMAL, "UDP_close");
    Fclose(fd);
}

/* The built-in gateway, reached through STinG rather than MiNTnet */
static void check_gateway(void)
{
    static const char ask[] = "GET / HTTP/1.1\r\nHost: webgate\r\n\r\n";
    static char answer[512];
    short cn = tcp_open(WEBGATE, 8280), got;
    int at = 0;

    check(cn > 0 && wait_established(cn, 2) == E_NORMAL, 1, "the gateway answers");
    call_wlw(TCP_SEND, cn, ask, sizeof ask - 1);

    while ((got = count_until(cn, 1)) > 0 && at + got < (int)sizeof answer)
        at += call_wlw(CNGET_BLOCK, cn, answer + at, got);

    check(got, E_EOF, "and closes when it has said its piece");
    check(strstr(answer, "webgate (tosemu) is running") != 0, 1, "which is who it is");
    tcp_close(cn);
}

int main(void)
{
    check_finding();
    if (!tpl)
    {
        printf("1..%d\n", n);
        return 1;
    }

    check_small_things();
    check_names();
    check_connection();
    check_datagrams();
    check_gateway();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
