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
 * STinG, as its TRANSPORT_TCPIP layer.
 *
 * The STiK cookie points at a DRV_LIST, whose get_dftab hands out the TPL: a
 * table of Pure C functions, called with their arguments on the stack as
 * packed words and longs and answering in d0. Each entry here is a line-A
 * opcode of its own followed by an rts, so a call lands in sting_linea with
 * the return address at 0(sp) and the arguments from 4(sp).
 *
 * A connection is a host socket, non-blocking throughout. What a program
 * sends goes into an output buffer of the size it asked for, all or nothing
 * the way STinG takes it, and drains from there whenever STinG is called or
 * anything waits - see net_wait.
 */

/* accept4 */
#define _GNU_SOURCE

#include "sting.h"

#include "config.h"
#include "cpu.h"
#include "m68k.h"
#include "net.h"
#include "tossystem.h"

#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

/* STinG's answers */
#define E_NORMAL        (0)
#define E_OBUFFULL      (-1)
#define E_NODATA        (-2)
#define E_EOF           (-3)
#define E_RRESET        (-4)
#define E_NOMEM         (-6)
#define E_REFUSE        (-7)
#define E_BADHANDLE     (-9)
#define E_LISTEN        (-10)
#define E_CONNECTFAIL   (-13)
#define E_CNTIMEOUT     (-16)
#define E_CANTRESOLVE   (-17)
#define E_UNREACHABLE   (-24)
#define E_NOROUTINE     (-26)
#define E_PARAMETER     (-30)
#define E_BIGBUF        (-31)

static const char *const error_text[] = {
    "No error",
    "Output buffer is full",
    "No data available",
    "EOF from remote",
    "Reset received",
    "Unacceptable packet received, reset",
    "Something failed due to lack of memory",
    "Connection refused by remote host",
    "A SYN was received in the window",
    "Bad connection handle used",
    "The connection is in LISTEN state",
    "No free CCB's available",
    "Connection does not exist",
    "Could not connect to remote socket",
    "Invalid TCP_close() requested",
    "User timeout expired",
    "Connection timed out",
    "Domain name or dotted dec. bad",
    "Domain name: Bad character",
    "Hardware error, carrier lost",
    "Hostname does not exist",
    "Resolver work load limit reached",
    "No nameservers could be found",
    "Bad format of DNS reply",
    "Destination unreachable",
    "Dns reply contained no address",
    "Routine is not available",
    "Locked by another application",
    "Error during fragmentation",
    "Time To Live of an IP packet exceeded",
    "Problem with a parameter",
    "Input buffer is too small for data",
    "Function not available",
};
#define ERRORS (int)(sizeof error_text / sizeof error_text[0])

/* The TPL's functions, in their order in the table */
enum {
    KRMALLOC, KRFREE, KRGETFREE, KRREALLOC, GET_ERR_TEXT, GETVSTR,
    CARRIER_DETECT, TCP_OPEN, TCP_CLOSE, TCP_SEND, TCP_WAIT_STATE,
    TCP_ACK_WAIT, UDP_OPEN, UDP_CLOSE, UDP_SEND, CNKICK, CNBYTE_COUNT,
    CNGET_CHAR, CNGET_NDB, CNGET_BLOCK, HOUSEKEEP, RESOLVE, SER_DISABLE,
    SER_ENABLE, SET_FLAG, CLEAR_FLAG, CNGETINFO, ON_PORT, OFF_PORT,
    SETVSTR, QUERY_PORT, CNGETS, ICMP_SEND, ICMP_HANDLER, ICMP_DISCARD,
    TCP_INFO, CNTRL_PORT,
    /* STinG's later additions and room past them, all refused, so that a
     * program reaching further than this knows about lands somewhere */
    TPL_FUNCTIONS = 48
};

/* The two that are the driver list's rather than the TPL's */
#define GET_DFTAB (0xf0)
#define ETM_EXEC  (0xf1)

#define OPCODE_BASE (0xa100)

/* Arguments to TCP_open that are not an address */
#define TCP_ACTIVE  (0x0000)
#define TCP_PASSIVE (0xffff)

#define TESTABLISH (4)

#define CONNECTIONS (32)
#define FLAGS       (64)
#define CIB_SIZE    (32)
#define POOL_SIZE   (16384)
#define POOL_BLOCKS (256)

struct connection
{
    int used;
    int fd;
    int udp;
    int listening;      /* fd is a listener until somebody connects */
    int connected;
    int closing;        /* closed by the program, still draining */
    int err;            /* the STinG error it failed with, once it has */
    uint8_t *out;
    int out_size;
    int out_len;
    uint32_t cib;
};

static struct connection conns[CONNECTIONS];

static uint32_t tpl, drv_list, error_strings[ERRORS], zero_string;
static uint8_t flags[FLAGS];

/* KRmalloc's pool, kept as blocks in address order */
static struct { uint32_t at, size; int used; } blocks[POOL_BLOCKS];
static int nblocks;

/* Guest memory ***************************************************************/

static void copy_out(uint32_t to, const void *from, uint32_t n)
{
    const uint8_t *p = from;
    uint32_t i;

    for (i = 0; i < n; i++)
        m68k_write_memory_8(to + i, p[i]);
}

static void copy_in(void *to, uint32_t from, uint32_t n)
{
    uint8_t *p = to;
    uint32_t i;

    for (i = 0; i < n; i++)
        p[i] = m68k_read_memory_8(from + i);
}

static void string_in(char *to, uint32_t from, size_t size)
{
    size_t i;

    for (i = 0; i + 1 < size; i++)
        if ((to[i] = m68k_read_memory_8(from + i)) == 0)
            return;
    to[i] = 0;
}

static uint32_t string_out(const char *s)
{
    uint32_t at = bios_static_alloc(strlen(s) + 1);

    if (at)
        copy_out(at, s, strlen(s) + 1);

    return at;
}

/* KRmalloc ******************************************************************/

static void pool_init(void)
{
    uint32_t size = POOL_SIZE, at = 0;

    while (size >= 1024 && (at = bios_static_alloc(size)) == 0)
        size /= 2;

    nblocks = 0;
    if (at)
    {
        blocks[0].at = at;
        blocks[0].size = size;
        blocks[0].used = 0;
        nblocks = 1;
    }
}

static uint32_t kr_malloc(uint32_t size)
{
    int i;

    size = (size + 3) & ~3u;
    if (size == 0)
        size = 4;

    for (i = 0; i < nblocks; i++)
    {
        if (blocks[i].used || blocks[i].size < size)
            continue;

        if (blocks[i].size > size && nblocks < POOL_BLOCKS)
        {
            memmove(&blocks[i + 2], &blocks[i + 1], (nblocks - i - 1) * sizeof blocks[0]);
            blocks[i + 1].at = blocks[i].at + size;
            blocks[i + 1].size = blocks[i].size - size;
            blocks[i + 1].used = 0;
            blocks[i].size = size;
            nblocks++;
        }
        blocks[i].used = 1;
        return blocks[i].at;
    }

    return 0;
}

static int kr_find(uint32_t at)
{
    int i;

    for (i = 0; i < nblocks; i++)
        if (blocks[i].at == at && blocks[i].used)
            return i;
    return -1;
}

static void kr_free(uint32_t at)
{
    int i = kr_find(at);

    if (i < 0)
        return;

    blocks[i].used = 0;

    if (i + 1 < nblocks && !blocks[i + 1].used)
    {
        blocks[i].size += blocks[i + 1].size;
        memmove(&blocks[i + 1], &blocks[i + 2], (nblocks - i - 2) * sizeof blocks[0]);
        nblocks--;
    }
    if (i > 0 && !blocks[i - 1].used)
    {
        blocks[i - 1].size += blocks[i].size;
        memmove(&blocks[i], &blocks[i + 1], (nblocks - i - 1) * sizeof blocks[0]);
        nblocks--;
    }
}

static uint32_t kr_getfree(int total)
{
    uint32_t n = 0;
    int i;

    for (i = 0; i < nblocks; i++)
        if (!blocks[i].used)
            n = total ? n + blocks[i].size : (blocks[i].size > n ? blocks[i].size : n);
    return n;
}

static uint32_t kr_realloc(uint32_t at, uint32_t size)
{
    uint32_t to, old;
    uint8_t *tmp;
    int i;

    if (at == 0)
        return kr_malloc(size);
    if (size == 0)
    {
        kr_free(at);
        return 0;
    }
    if ((i = kr_find(at)) < 0)
        return 0;
    old = blocks[i].size;
    if ((to = kr_malloc(size)) == 0)
        return 0;

    if (size > old)
        size = old;
    tmp = malloc(size);
    if (tmp)
    {
        copy_in(tmp, at, size);
        copy_out(to, tmp, size);
        free(tmp);
    }
    kr_free(at);

    return to;
}

/* Connections ***************************************************************/

static int sting_error(int err)
{
    switch (err)
    {
    case ECONNREFUSED:
        return E_REFUSE;
    case ETIMEDOUT:
        return E_CNTIMEOUT;
    case ECONNRESET:
    case EPIPE:
        return E_RRESET;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ENETDOWN:
    case EHOSTDOWN:
        return E_UNREACHABLE;
    case ENOMEM:
    case ENOBUFS:
        return E_NOMEM;
    default:
        return E_CONNECTFAIL;
    }
}

static struct connection *connection(int16_t cn)
{
    if (cn < 1 || cn > CONNECTIONS || !conns[cn - 1].used || conns[cn - 1].closing)
        return NULL;
    return &conns[cn - 1];
}

static void drop(struct connection *c)
{
    if (c->fd >= 0)
        close(c->fd);
    free(c->out);
    memset(c, 0, sizeof *c);
    c->fd = -1;
}

static int16_t new_connection(int fd, int udp, int out_size)
{
    int i;

    for (i = 0; i < CONNECTIONS; i++)
    {
        struct connection *c = &conns[i];

        if (c->used)
            continue;

        memset(c, 0, sizeof *c);
        c->out = out_size > 0 ? malloc(out_size) : NULL;
        if (out_size > 0 && !c->out)
            return E_NOMEM;
        c->used = 1;
        c->fd = fd;
        c->udp = udp;
        c->out_size = out_size;
        return i + 1;
    }

    return E_NOMEM;
}

/* Whether a connection under way has been made, which a socket says by being
 * writable */
static void check_connected(struct connection *c)
{
    struct pollfd p = { c->fd, POLLOUT, 0 };
    int err;

    if (c->connected || c->listening || c->err)
        return;
    if (poll(&p, 1, 0) <= 0)
        return;

    err = net_socket_error(c->fd);
    if (err)
        c->err = sting_error(-err);
    else
        c->connected = 1;
}

/* A passive connection takes the first that arrives, and stops listening */
static void check_accept(struct connection *c)
{
    int fd;

    if (!c->listening)
        return;

    fd = accept4(c->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0)
        return;

    close(c->fd);
    c->fd = fd;
    c->listening = 0;
    c->connected = 1;
}

static void flush_one(struct connection *c)
{
    check_accept(c);
    check_connected(c);

    while (c->connected && !c->err && c->out_len > 0)
    {
        ssize_t r = send(c->fd, c->out, c->out_len, MSG_NOSIGNAL | MSG_DONTWAIT);

        if (r < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                c->err = sting_error(errno);
            break;
        }
        memmove(c->out, c->out + r, c->out_len - r);
        c->out_len -= r;
    }

    if (c->closing && (c->out_len == 0 || c->err || !c->connected))
        drop(c);
}

void sting_flush(void)
{
    int i;

    for (i = 0; i < CONNECTIONS; i++)
        if (conns[i].used)
            flush_one(&conns[i]);
}

void sting_reset(void)
{
    int i;

    for (i = 0; i < CONNECTIONS; i++)
        if (conns[i].used)
            drop(&conns[i]);
}

/* What a connection has to read: the bytes, 0, or an error */
static int16_t byte_count(struct connection *c)
{
    int n = 0;
    char b;
    ssize_t r;

    check_accept(c);
    check_connected(c);

    if (c->listening)
        return E_LISTEN;
    if (c->err)
        return c->err;
    if (!c->connected)
        return 0;

    if (ioctl(c->fd, FIONREAD, &n) < 0)
        return sting_error(errno);
    if (n > 0)
        return n > 32767 ? 32767 : n;
    if (c->udp)
        return 0;

    r = recv(c->fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (r == 0)
        return E_EOF;
    if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        c->err = sting_error(errno);
        return c->err;
    }

    return 0;
}

/* The calls *****************************************************************/

static int32_t tcp_open(uint32_t host, uint16_t port, uint16_t obsize)
{
    struct sockaddr_in sa;
    int fd, r, passive = 0;
    int16_t cn;

    /* A CAB in place of an address: lport, rport, rhost, lhost */
    if (port == TCP_ACTIVE || port == TCP_PASSIVE)
    {
        uint32_t cab = host;

        passive = port == TCP_PASSIVE;
        port = passive ? m68k_read_memory_16(cab) : m68k_read_memory_16(cab + 2);
        host = m68k_read_memory_32(cab + 4);
    }
    else if (host == 0)
        passive = 1;

    if (obsize == 0)
        obsize = 4096;

    fd = net_socket(SOCK_STREAM);
    if (fd < 0)
        return sting_error(-fd);

    if (passive)
    {
        int one = 1;

        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(fd, 1) < 0)
        {
            r = sting_error(errno);
            close(fd);
            return r;
        }
        r = 0;
    }
    else
    {
        r = net_connect(fd, host, port);
        if (r < 0 && r != -EINPROGRESS)
        {
            close(fd);
            return sting_error(-r);
        }
    }

    cn = new_connection(fd, 0, obsize);
    if (cn < 0)
    {
        close(fd);
        return cn;
    }

    conns[cn - 1].listening = passive;
    conns[cn - 1].connected = !passive && r == 0;

    return cn;
}

static int32_t udp_open(uint32_t host, uint16_t port)
{
    int fd, r;
    int16_t cn;

    if (host == NET_GATEWAY_IP)
        return E_UNREACHABLE;

    fd = net_socket(SOCK_DGRAM);
    if (fd < 0)
        return sting_error(-fd);

    r = net_connect(fd, host, port);
    if (r < 0)
    {
        close(fd);
        return sting_error(-r);
    }

    cn = new_connection(fd, 1, 0);
    if (cn < 0)
    {
        close(fd);
        return cn;
    }
    conns[cn - 1].connected = 1;

    return cn;
}

static int32_t tcp_send(int16_t cn, uint32_t buf, int16_t len)
{
    struct connection *c = connection(cn);

    if (!c || c->udp)
        return E_BADHANDLE;
    if (len < 0)
        return E_PARAMETER;

    flush_one(c);
    if (c->err)
        return c->err;
    if (len > c->out_size)
        return E_BIGBUF;
    if (c->out_size - c->out_len < len)
        return E_OBUFFULL;

    copy_in(c->out + c->out_len, buf, len);
    c->out_len += len;
    flush_one(c);

    return E_NORMAL;
}

static int32_t udp_send(int16_t cn, uint32_t buf, int16_t len)
{
    struct connection *c = connection(cn);
    uint8_t *tmp;
    ssize_t r;

    if (!c || !c->udp)
        return E_BADHANDLE;
    if (len < 0)
        return E_PARAMETER;

    tmp = malloc(len ? len : 1);
    if (!tmp)
        return E_NOMEM;
    copy_in(tmp, buf, len);
    r = send(c->fd, tmp, len, MSG_NOSIGNAL | MSG_DONTWAIT);
    free(tmp);

    if (r < 0)
        return errno == EAGAIN || errno == EWOULDBLOCK ? E_OBUFFULL : sting_error(errno);

    return E_NORMAL;
}

static int32_t tcp_wait_state(int16_t cn, int16_t state, int16_t timeout)
{
    struct connection *c = connection(cn);

    if (!c || c->udp)
        return E_BADHANDLE;
    if (state != TESTABLISH)
        return E_NORMAL;

    flush_one(c);
    if (c->err)
        return c->err;

    if (c->listening)
    {
        net_wait(c->fd, POLLIN, timeout * 1000L);
        check_accept(c);
        return c->connected ? E_NORMAL : E_CNTIMEOUT;
    }

    if (!c->connected)
    {
        if (net_wait(c->fd, POLLOUT, timeout * 1000L) > 0)
            check_connected(c);
    }

    if (c->err)
        return c->err;
    return c->connected ? E_NORMAL : E_CNTIMEOUT;
}

/* Until what was sent has gone, or timeout milliseconds */
static void drain(struct connection *c, long timeout)
{
    flush_one(c);

    while (c->used && c->out_len > 0 && !c->err && c->connected)
    {
        int r = net_wait(c->fd, POLLOUT, timeout);

        if (r <= 0)
            break;
        flush_one(c);
    }
}

static int32_t tcp_close(int16_t cn, int16_t timeout, uint32_t result)
{
    struct connection *c = connection(cn);

    if (!c)
        return E_BADHANDLE;

    if (timeout > 0)
        drain(c, timeout * 1000L);

    if (result)
        m68k_write_memory_16(result, E_NORMAL);

    c->closing = 1;
    flush_one(c);

    return E_NORMAL;
}

static int32_t get_block(int16_t cn, uint32_t buf, int16_t len)
{
    struct connection *c = connection(cn);
    int16_t n;
    uint8_t *tmp;
    ssize_t r;

    if (!c)
        return E_BADHANDLE;
    if (len <= 0)
        return len == 0 ? 0 : E_PARAMETER;

    n = byte_count(c);
    if (n < 0)
        return n;
    if (n < len && !(c->udp && n > 0))
        return E_NODATA;

    tmp = malloc(len);
    if (!tmp)
        return E_NOMEM;

    r = recv(c->fd, tmp, len, MSG_DONTWAIT);
    if (r < 0)
    {
        free(tmp);
        return sting_error(errno);
    }

    copy_out(buf, tmp, r);
    free(tmp);

    return (int32_t)r;
}

static int32_t get_char(int16_t cn)
{
    struct connection *c = connection(cn);
    int16_t n;
    uint8_t b;

    if (!c)
        return E_BADHANDLE;

    n = byte_count(c);
    if (n <= 0)
        return n == 0 ? E_NODATA : n;

    if (recv(c->fd, &b, 1, MSG_DONTWAIT) != 1)
        return E_NODATA;

    return b;
}

/* NDB: ptr, ndata, len, next - the data in a block of its own */
static uint32_t get_ndb(int16_t cn)
{
    struct connection *c = connection(cn);
    uint32_t ndb, data;
    uint8_t *tmp;
    int16_t n;
    ssize_t r;

    if (!c || (n = byte_count(c)) <= 0)
        return 0;

    if (n > 1500)
        n = 1500;

    ndb = kr_malloc(14);
    data = kr_malloc(n);
    tmp = malloc(n);
    if (!ndb || !data || !tmp)
    {
        kr_free(ndb);
        kr_free(data);
        free(tmp);
        return 0;
    }

    r = recv(c->fd, tmp, n, MSG_DONTWAIT);
    if (r <= 0)
    {
        kr_free(ndb);
        kr_free(data);
        free(tmp);
        return 0;
    }

    copy_out(data, tmp, r);
    free(tmp);

    m68k_write_memory_32(ndb, data);
    m68k_write_memory_32(ndb + 4, data);
    m68k_write_memory_16(ndb + 8, r);
    m68k_write_memory_32(ndb + 10, 0);

    return ndb;
}

/* Up to the delimiter, which becomes the string's end */
static int32_t get_s(int16_t cn, uint32_t buf, int16_t len, uint8_t delim)
{
    struct connection *c = connection(cn);
    uint8_t *tmp;
    int16_t n;
    ssize_t r;
    int i;

    if (!c)
        return E_BADHANDLE;
    if (len <= 0)
        return E_PARAMETER;

    n = byte_count(c);
    if (n <= 0)
        return n == 0 ? E_NODATA : n;

    tmp = malloc(len);
    if (!tmp)
        return E_NOMEM;

    r = recv(c->fd, tmp, len, MSG_PEEK | MSG_DONTWAIT);
    for (i = 0; i < r; i++)
        if (tmp[i] == delim)
            break;

    if (i == r)
    {
        free(tmp);
        return r >= len ? E_BIGBUF : E_NODATA;
    }

    r = recv(c->fd, tmp, i + 1, MSG_DONTWAIT);
    tmp[i] = 0;
    copy_out(buf, tmp, i + 1);
    free(tmp);

    return i;
}

static int32_t resolve(uint32_t name, uint32_t real_name, uint32_t list, int16_t size)
{
    char host[256];
    uint32_t ip;

    string_in(host, name, sizeof host);

    if (host[0] == 0 || net_resolve(host, &ip) < 0)
        return E_CANTRESOLVE;

    if (real_name)
    {
        uint32_t copy = kr_malloc(strlen(host) + 1);

        if (copy)
            copy_out(copy, host, strlen(host) + 1);
        m68k_write_memory_32(real_name, copy);
    }

    if (list && size > 0)
        m68k_write_memory_32(list, ip);

    return 1;
}

/* CIB: protocol, lport, rport, rhost, lhost, status */
static uint32_t get_info(int16_t cn)
{
    struct connection *c = connection(cn);
    struct sockaddr_in local, remote;
    socklen_t len;

    if (!c)
        return 0;
    if (!c->cib && !(c->cib = kr_malloc(CIB_SIZE)))
        return 0;

    memset(&local, 0, sizeof local);
    memset(&remote, 0, sizeof remote);
    len = sizeof local;
    getsockname(c->fd, (struct sockaddr *)&local, &len);
    len = sizeof remote;
    getpeername(c->fd, (struct sockaddr *)&remote, &len);

    m68k_write_memory_16(c->cib, c->udp ? IPPROTO_UDP : IPPROTO_TCP);
    m68k_write_memory_16(c->cib + 2, ntohs(local.sin_port));
    m68k_write_memory_16(c->cib + 4, ntohs(remote.sin_port));
    m68k_write_memory_32(c->cib + 6, ntohl(remote.sin_addr.s_addr));
    m68k_write_memory_32(c->cib + 10, ntohl(local.sin_addr.s_addr));
    m68k_write_memory_16(c->cib + 14, 0);

    return c->cib;
}

static int32_t call(int fn)
{
    struct connection *c;
    char name[32];

    switch (fn)
    {
    case GET_DFTAB:
        string_in(name, peek_u32(4), sizeof name);
        return strcmp(name, "TRANSPORT_TCPIP") == 0 ? tpl : 0;
    case ETM_EXEC:
        return 0;

    case KRMALLOC:
        return kr_malloc(peek_u32(4));
    case KRFREE:
        kr_free(peek_u32(4));
        return 0;
    case KRGETFREE:
        return kr_getfree(peek_u16(4));
    case KRREALLOC:
        return kr_realloc(peek_u32(4), peek_u32(8));
    case GET_ERR_TEXT:
    {
        int16_t e = peek_s16(4);

        if (e > 0)
            e = -e;
        return -e < ERRORS ? error_strings[-e] : error_strings[ERRORS - 1];
    }
    case GETVSTR:
        return zero_string;
    case CARRIER_DETECT:
        return 1;
    case TCP_OPEN:
        return tcp_open(peek_u32(4), peek_u16(8), peek_u16(12));
    case TCP_CLOSE:
        return tcp_close(peek_s16(4), peek_s16(6), peek_u32(8));
    case TCP_SEND:
        return tcp_send(peek_s16(4), peek_u32(6), peek_s16(10));
    case TCP_WAIT_STATE:
        return tcp_wait_state(peek_s16(4), peek_s16(6), peek_s16(8));
    case TCP_ACK_WAIT:
        if (!(c = connection(peek_s16(4))))
            return E_BADHANDLE;
        drain(c, peek_s16(6));
        return c->err ? c->err : E_NORMAL;
    case UDP_OPEN:
        return udp_open(peek_u32(4), peek_u16(8));
    case UDP_CLOSE:
        if (!(c = connection(peek_s16(4))) || !c->udp)
            return E_BADHANDLE;
        drop(c);
        return E_NORMAL;
    case UDP_SEND:
        return udp_send(peek_s16(4), peek_u32(6), peek_s16(10));
    case CNKICK:
        return connection(peek_s16(4)) ? E_NORMAL : E_BADHANDLE;
    case CNBYTE_COUNT:
        if (!(c = connection(peek_s16(4))))
            return E_BADHANDLE;
        return byte_count(c);
    case CNGET_CHAR:
        return get_char(peek_s16(4));
    case CNGET_NDB:
        return get_ndb(peek_s16(4));
    case CNGET_BLOCK:
        return get_block(peek_s16(4), peek_u32(6), peek_s16(10));
    case HOUSEKEEP:
    case SER_DISABLE:
    case SER_ENABLE:
        return 0;
    case RESOLVE:
        return resolve(peek_u32(4), peek_u32(8), peek_u32(12), peek_s16(16));
    case SET_FLAG:
    {
        uint16_t f = peek_u16(4);
        int was;

        if (f >= FLAGS)
            return 1;
        was = flags[f];
        flags[f] = 1;
        return was;
    }
    case CLEAR_FLAG:
        if (peek_u16(4) < FLAGS)
            flags[peek_u16(4)] = 0;
        return 0;
    case CNGETINFO:
        return get_info(peek_s16(4));
    case CNGETS:
        return get_s(peek_s16(4), peek_u32(6), peek_s16(10), peek_u16(12) & 0xff);
    }

    if (verbose >= VERBOSE_CALLS)
        printf("STinG function %d is not there\n", fn);

    return E_NOROUTINE;
}

int sting_linea(unsigned int opcode)
{
    int fn = opcode & 0xff;
    int32_t r;

    if ((opcode & 0xff00) != OPCODE_BASE || !tpl
        || (fn >= TPL_FUNCTIONS && fn != GET_DFTAB && fn != ETM_EXEC))
        return 0;

    sting_flush();
    r = call(fn);

    if (verbose >= VERBOSE_CALLS)
        printf("STinG %d: %d = 0x%x\n", fn, r, (uint32_t)r);

    m68k_set_reg(M68K_REG_D0, (uint32_t)r);

    return 1;
}

/* Setting up ***************************************************************/

static uint32_t stub(int fn)
{
    uint32_t at = bios_static_alloc(4);

    if (at)
    {
        m68k_write_memory_16(at, OPCODE_BASE | fn);
        m68k_write_memory_16(at + 2, 0x4e75); /* rts */
    }
    return at;
}

void sting_init(void)
{
    uint32_t table, cfg;
    int i;

    tpl = 0;
    if (!net_enabled())
        return;

    /* Everything here is rebuilt with the machine, and the pool with it */
    pool_init();

    for (i = 0; i < ERRORS; i++)
        error_strings[i] = string_out(error_text[i]);
    zero_string = string_out("0");

    /* module, author, version, then the functions */
    table = bios_static_alloc(12 + TPL_FUNCTIONS * 4);
    cfg = bios_static_alloc(256);
    drv_list = bios_static_alloc(26);
    if (!table || !cfg || !drv_list)
        return;

    m68k_write_memory_32(table, string_out("TRANSPORT_TCPIP"));
    m68k_write_memory_32(table + 4, string_out("tosemu"));
    m68k_write_memory_32(table + 8, string_out("01.20"));
    for (i = 0; i < TPL_FUNCTIONS; i++)
        m68k_write_memory_32(table + 12 + i * 4, stub(i));

    for (i = 0; i < 256; i += 2)
        m68k_write_memory_16(cfg + i, 0);

    copy_out(drv_list, "STiKmagic", 10);
    m68k_write_memory_32(drv_list + 10, stub(GET_DFTAB));
    m68k_write_memory_32(drv_list + 14, stub(ETM_EXEC));
    m68k_write_memory_32(drv_list + 18, cfg);
    m68k_write_memory_32(drv_list + 22, 0);

    tpl = table;
    cookie_add(0x5354694bu, drv_list);  /* STiK */
}
