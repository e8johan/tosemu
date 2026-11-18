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
 * MiNTnet's socket calls. A socket is a GEMDOS handle whose open file is a
 * host socket, so Fread, Fwrite, Fclose, Fdup and the closing of what a
 * program left open all work on it - see gemdosfile.c.
 *
 * The host socket is always non-blocking. A call the program expects to
 * block waits in net_wait instead, which keeps the machine's clock going.
 */

/* accept4 */
#define _GNU_SOURCE

#include "gemdos.h"
#include "gemdos_p.h"
#include "gemdosfile_p.h"
#include "gemdosnet_p.h"

#include "cpu.h"
#include "m68k.h"
#include "net.h"
#include "tossystem.h"

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

/* MiNT's numbers, from its errno.h, sys/socket.h and fcntl.h */
#define M_EBADF         (-37)
#define M_EMFILE        (-35)
#define M_ENOMEM        (-39)
#define M_EINVAL        (-25)
#define M_EINTR         (-128)
#define M_EPIPE         (-81)
#define M_ENOTSOCK      (-300)
#define M_EDESTADDRREQ  (-301)
#define M_EMSGSIZE      (-302)
#define M_ENOPROTOOPT   (-304)
#define M_EPROTONOSUPPORT (-305)
#define M_ESOCKTNOSUPPORT (-306)
#define M_EOPNOTSUPP    (-307)
#define M_EAFNOSUPPORT  (-309)
#define M_EADDRINUSE    (-310)
#define M_EADDRNOTAVAIL (-311)
#define M_ENETDOWN      (-312)
#define M_ENETUNREACH   (-313)
#define M_ECONNABORTED  (-315)
#define M_ECONNRESET    (-316)
#define M_EISCONN       (-317)
#define M_ENOTCONN      (-318)
#define M_ESHUTDOWN     (-319)
#define M_ETIMEDOUT     (-320)
#define M_ECONNREFUSED  (-321)
#define M_EHOSTDOWN     (-322)
#define M_EHOSTUNREACH  (-323)
#define M_EALREADY      (-324)
#define M_EINPROGRESS   (-325)
#define M_EAGAIN        (-326)
#define M_ENOBUFS       (-327)

#define M_AF_INET       (2)
#define M_SOCK_STREAM   (1)
#define M_SOCK_DGRAM    (2)

#define M_SOL_SOCKET    (0xffff)
#define M_SO_REUSEADDR  (2)
#define M_SO_TYPE       (3)
#define M_SO_ERROR      (4)
#define M_SO_BROADCAST  (6)
#define M_SO_SNDBUF     (7)
#define M_SO_RCVBUF     (8)
#define M_SO_KEEPALIVE  (9)
#define M_SO_OOBINLINE  (10)
#define M_TCP_NODELAY   (1)

#define M_MSG_OOB       (0x01)
#define M_MSG_PEEK      (0x02)
#define M_MSG_DONTROUTE (0x04)
#define M_MSG_WAITALL   (0x40)
#define M_MSG_DONTWAIT  (0x80)

#define M_F_GETFL       (3)
#define M_F_SETFL       (4)
#define M_FIONREAD      (('F' << 8) | 1)
#define M_FIONWRITE     (('F' << 8) | 2)
#define M_O_RDWR        (2)
#define M_O_NDELAY      (0x100)

/* sockaddr_in: family, port, address, eight bytes of nothing */
#define SOCKADDR_SIZE   (16)

static int32_t mint_error(int err)
{
    switch (err)
    {
    case EBADF:         return M_EBADF;
    case ENOMEM:        return M_ENOMEM;
    case EINVAL:        return M_EINVAL;
    case EINTR:         return M_EINTR;
    case EPIPE:         return M_EPIPE;
    case ENOTSOCK:      return M_ENOTSOCK;
    case EDESTADDRREQ:  return M_EDESTADDRREQ;
    case EMSGSIZE:      return M_EMSGSIZE;
    case ENOPROTOOPT:   return M_ENOPROTOOPT;
    case EOPNOTSUPP:    return M_EOPNOTSUPP;
    case EAFNOSUPPORT:  return M_EAFNOSUPPORT;
    case EADDRINUSE:    return M_EADDRINUSE;
    case EADDRNOTAVAIL: return M_EADDRNOTAVAIL;
    case ENETDOWN:      return M_ENETDOWN;
    case ENETUNREACH:   return M_ENETUNREACH;
    case ECONNABORTED:  return M_ECONNABORTED;
    case ECONNRESET:    return M_ECONNRESET;
    case EISCONN:       return M_EISCONN;
    case ENOTCONN:      return M_ENOTCONN;
    case ESHUTDOWN:     return M_ESHUTDOWN;
    case ETIMEDOUT:     return M_ETIMEDOUT;
    case ECONNREFUSED:  return M_ECONNREFUSED;
    case EHOSTDOWN:     return M_EHOSTDOWN;
    case EHOSTUNREACH:  return M_EHOSTUNREACH;
    case EALREADY:      return M_EALREADY;
    case EINPROGRESS:   return M_EINPROGRESS;
    case EAGAIN:        return M_EAGAIN;
    case ENOBUFS:       return M_ENOBUFS;
    default:            return GEMDOS_EINTRN;
    }
}

/* The host socket behind a handle, or the error for one that is not */
static int socket_of(uint16_t h, uint32_t **sflags, int32_t *err)
{
    int fd = gemdos_socket_fd(h, sflags);

    *err = fd == -1 ? M_ENOTSOCK : M_EBADF;

    return fd;
}

static int read_sockaddr(uint32_t at, uint32_t len, uint32_t *ip, uint16_t *port)
{
    if (at == 0 || len < 8)
        return M_EINVAL;
    if (m68k_read_memory_16(at) != M_AF_INET)
        return M_EAFNOSUPPORT;

    *port = m68k_read_memory_16(at + 2);
    *ip = m68k_read_memory_32(at + 4);
    return 0;
}

/* The address into a program's sockaddr, with its length where lenp says */
static void write_sockaddr(uint32_t at, uint32_t lenp, const struct sockaddr_in *sa)
{
    int i;

    if (at == 0)
        return;

    m68k_write_memory_16(at, M_AF_INET);
    m68k_write_memory_16(at + 2, ntohs(sa->sin_port));
    m68k_write_memory_32(at + 4, ntohl(sa->sin_addr.s_addr));
    for (i = 8; i < SOCKADDR_SIZE; i++)
        m68k_write_memory_8(at + i, 0);

    if (lenp)
        m68k_write_memory_32(lenp, SOCKADDR_SIZE);
}

static int host_msg_flags(uint32_t flags)
{
    return (flags & M_MSG_OOB ? MSG_OOB : 0) | (flags & M_MSG_PEEK ? MSG_PEEK : 0)
         | (flags & M_MSG_DONTROUTE ? MSG_DONTROUTE : 0)
         | (flags & M_MSG_WAITALL ? MSG_WAITALL : 0);
}

/* Receives, waiting for something to arrive unless the program said not to.
 * from, if not NULL, is given the sender. */
static int32_t receive(int fd, int nonblock, uint32_t buf, uint32_t len, int flags,
                       struct sockaddr_in *from)
{
    uint8_t *tmp = malloc(len ? len : 1);
    socklen_t fromlen = sizeof *from;
    ssize_t r;

    if (!tmp)
        return M_ENOMEM;

    for (;;)
    {
        r = recvfrom(fd, tmp, len, flags | MSG_DONTWAIT, (struct sockaddr *)from,
                     from ? &fromlen : NULL);
        if (r >= 0)
            break;
        if (errno == EINTR)
            continue;
        if ((errno != EAGAIN && errno != EWOULDBLOCK) || nonblock)
        {
            int32_t err = mint_error(errno);

            free(tmp);
            return err;
        }
        if (net_wait(fd, POLLIN, -1) < 0)
        {
            free(tmp);
            return M_EINTR;
        }
    }

    {
        ssize_t i;

        for (i = 0; i < r; i++)
            m68k_write_memory_8(buf + i, tmp[i]);
    }
    free(tmp);

    return (int32_t)r;
}

/* Sends all of it, or as much as goes without waiting for a program that
 * asked not to wait */
static int32_t transmit(int fd, int nonblock, uint32_t buf, uint32_t len, int flags,
                        const struct sockaddr_in *to)
{
    uint8_t *tmp = malloc(len ? len : 1);
    uint32_t done = 0, i;

    if (!tmp)
        return M_ENOMEM;

    for (i = 0; i < len; i++)
        tmp[i] = m68k_read_memory_8(buf + i);

    do
    {
        ssize_t r = sendto(fd, tmp + done, len - done, flags | MSG_DONTWAIT | MSG_NOSIGNAL,
                           (const struct sockaddr *)to, to ? sizeof *to : 0);

        if (r >= 0)
        {
            done += r;
            continue;
        }
        if (errno == EINTR)
            continue;
        if ((errno != EAGAIN && errno != EWOULDBLOCK) || nonblock)
        {
            int32_t err = mint_error(errno);

            free(tmp);
            return done ? (int32_t)done : err;
        }
        if (net_wait(fd, POLLOUT, -1) < 0)
            break;
    } while (done < len);

    free(tmp);

    return (int32_t)done;
}

int32_t gemdos_socket_read(int fd, uint32_t sflags, uint32_t buf, uint32_t len)
{
    return receive(fd, sflags & M_O_NDELAY, buf, len, 0, NULL);
}

int32_t gemdos_socket_write(int fd, uint32_t sflags, uint32_t buf, uint32_t len)
{
    return transmit(fd, sflags & M_O_NDELAY, buf, len, 0, NULL);
}

/* The calls *****************************************************************/

uint32_t GEMDOS_Fsocket()
{
    uint32_t domain = peek_u32(2), type = peek_u32(6), protocol = peek_u32(10);
    int fd, h;

    FUNC_TRACE_ENTER_ARGS {
        printf("    domain: %u, type: %u, protocol: %u\n", domain, type, protocol);
    }

    if (!net_enabled())
        return GEMDOS_EINVFN;
    if (domain != M_AF_INET)
        return M_EAFNOSUPPORT;
    if (type != M_SOCK_STREAM && type != M_SOCK_DGRAM)
        return M_ESOCKTNOSUPPORT;
    if (protocol != 0
        && protocol != (type == M_SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP))
        return M_EPROTONOSUPPORT;

    fd = net_socket(type == M_SOCK_STREAM ? SOCK_STREAM : SOCK_DGRAM);
    if (fd < 0)
        return mint_error(-fd);

    h = gemdos_socket_open(fd);
    if (h < 0)
    {
        close(fd);
        return M_EMFILE;
    }

    return h;
}

uint32_t GEMDOS_Fconnect()
{
    uint16_t h = peek_u16(2);
    uint32_t addr = peek_u32(4), len = peek_u32(8);
    uint32_t *sflags, ip;
    uint16_t port;
    int32_t err;
    int fd, r;

    if ((fd = socket_of(h, &sflags, &err)) < 0)
        return err;
    if ((err = read_sockaddr(addr, len, &ip, &port)) < 0)
        return err;

    FUNC_TRACE_ENTER_ARGS {
        printf("    handle: %d, to %u.%u.%u.%u:%u\n", h, ip >> 24, (ip >> 16) & 255,
               (ip >> 8) & 255, ip & 255, port);
    }

    r = net_connect(fd, ip, port);
    if (r == 0)
        return 0;
    if (r != -EINPROGRESS)
        return mint_error(-r);
    if (*sflags & M_O_NDELAY)
        return M_EINPROGRESS;

    if (net_wait(fd, POLLOUT, -1) < 0)
        return M_EINTR;

    r = net_socket_error(fd);
    return r ? mint_error(-r) : 0;
}

uint32_t GEMDOS_Fbind()
{
    uint16_t h = peek_u16(2);
    uint32_t addr = peek_u32(4), len = peek_u32(8);
    struct sockaddr_in sa;
    uint32_t ip;
    uint16_t port;
    int32_t err;
    int fd;

    if ((fd = socket_of(h, NULL, &err)) < 0)
        return err;
    if ((err = read_sockaddr(addr, len, &ip, &port)) < 0)
        return err;

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(ip);

    return bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 ? mint_error(errno) : 0;
}

uint32_t GEMDOS_Flisten()
{
    int32_t err;
    int fd;

    if ((fd = socket_of(peek_u16(2), NULL, &err)) < 0)
        return err;

    return listen(fd, (int)peek_u32(4)) < 0 ? mint_error(errno) : 0;
}

uint32_t GEMDOS_Faccept()
{
    uint16_t h = peek_u16(2);
    uint32_t addr = peek_u32(4), lenp = peek_u32(8);
    struct sockaddr_in sa;
    socklen_t salen;
    uint32_t *sflags;
    int32_t err;
    int fd, conn, n;

    if ((fd = socket_of(h, &sflags, &err)) < 0)
        return err;

    for (;;)
    {
        salen = sizeof sa;
        conn = accept4(fd, (struct sockaddr *)&sa, &salen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (conn >= 0)
            break;
        if (errno == EINTR)
            continue;
        if ((errno != EAGAIN && errno != EWOULDBLOCK) || (*sflags & M_O_NDELAY))
            return mint_error(errno);
        if (net_wait(fd, POLLIN, -1) < 0)
            return M_EINTR;
    }

    n = gemdos_socket_open(conn);
    if (n < 0)
    {
        close(conn);
        return M_EMFILE;
    }

    write_sockaddr(addr, lenp, &sa);

    return n;
}

uint32_t GEMDOS_Frecvfrom()
{
    uint16_t h = peek_u16(2);
    uint32_t buf = peek_u32(4), len = peek_u32(8), flags = peek_u32(12);
    uint32_t from = peek_u32(16), fromlenp = peek_u32(20);
    struct sockaddr_in sa;
    uint32_t *sflags;
    int32_t err, r;
    int fd;

    if ((fd = socket_of(h, &sflags, &err)) < 0)
        return err;

    memset(&sa, 0, sizeof sa);
    r = receive(fd, (*sflags & M_O_NDELAY) || (flags & M_MSG_DONTWAIT), buf, len,
                host_msg_flags(flags), from ? &sa : NULL);
    if (r >= 0 && from)
        write_sockaddr(from, fromlenp, &sa);

    return r;
}

uint32_t GEMDOS_Fsendto()
{
    uint16_t h = peek_u16(2);
    uint32_t buf = peek_u32(4), len = peek_u32(8), flags = peek_u32(12);
    uint32_t to = peek_u32(16), tolen = peek_u32(20);
    struct sockaddr_in sa;
    uint32_t *sflags, ip;
    uint16_t port;
    int32_t err;
    int fd;

    if ((fd = socket_of(h, &sflags, &err)) < 0)
        return err;

    if (to)
    {
        if ((err = read_sockaddr(to, tolen, &ip, &port)) < 0)
            return err;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = htonl(ip);
    }

    return transmit(fd, (*sflags & M_O_NDELAY) || (flags & M_MSG_DONTWAIT), buf, len,
                    host_msg_flags(flags), to ? &sa : NULL);
}

/* A MiNT option as the host's, or 0 for one this does not pass on */
static int host_option(uint32_t level, uint32_t name, int *hlevel, int *hname)
{
    if (level == M_SOL_SOCKET)
    {
        *hlevel = SOL_SOCKET;
        switch (name)
        {
        case M_SO_REUSEADDR: *hname = SO_REUSEADDR; return 1;
        case M_SO_TYPE:      *hname = SO_TYPE;      return 1;
        case M_SO_ERROR:     *hname = SO_ERROR;     return 1;
        case M_SO_BROADCAST: *hname = SO_BROADCAST; return 1;
        case M_SO_SNDBUF:    *hname = SO_SNDBUF;    return 1;
        case M_SO_RCVBUF:    *hname = SO_RCVBUF;    return 1;
        case M_SO_KEEPALIVE: *hname = SO_KEEPALIVE; return 1;
        case M_SO_OOBINLINE: *hname = SO_OOBINLINE; return 1;
        }
    }
    else if (level == IPPROTO_TCP && name == M_TCP_NODELAY)
    {
        *hlevel = IPPROTO_TCP;
        *hname = TCP_NODELAY;
        return 1;
    }

    return 0;
}

/* An option's value, a short or a long as the program sized it */
static int32_t option_in(uint32_t val, uint32_t size)
{
    if (size == 2)
        return (int16_t)m68k_read_memory_16(val);
    return (int32_t)m68k_read_memory_32(val);
}

uint32_t GEMDOS_Fsetsockopt()
{
    uint16_t h = peek_u16(2);
    uint32_t level = peek_u32(4), name = peek_u32(8), val = peek_u32(12), size = peek_u32(16);
    int32_t err;
    int fd, hlevel, hname, v;

    if ((fd = socket_of(h, NULL, &err)) < 0)
        return err;

    /* One the host has no use for is taken and forgotten, as an option that
     * changes nothing here would be */
    if (!host_option(level, name, &hlevel, &hname) || val == 0 || size < 2)
        return 0;

    v = option_in(val, size);
    return setsockopt(fd, hlevel, hname, &v, sizeof v) < 0 ? mint_error(errno) : 0;
}

uint32_t GEMDOS_Fgetsockopt()
{
    uint16_t h = peek_u16(2);
    uint32_t level = peek_u32(4), name = peek_u32(8), val = peek_u32(12), sizep = peek_u32(16);
    socklen_t len = sizeof(int);
    int32_t err;
    int fd, hlevel, hname, v = 0;

    if ((fd = socket_of(h, NULL, &err)) < 0)
        return err;
    if (!host_option(level, name, &hlevel, &hname))
        return M_ENOPROTOOPT;

    if (getsockopt(fd, hlevel, hname, &v, &len) < 0)
        return mint_error(errno);

    /* MiNT's numbers are its own: an error as its errno, positive, and a
     * type as its SOCK_ */
    if (hname == SO_ERROR && hlevel == SOL_SOCKET)
        v = v ? -mint_error(v) : 0;
    else if (hname == SO_TYPE && hlevel == SOL_SOCKET)
        v = v == SOCK_STREAM ? M_SOCK_STREAM : M_SOCK_DGRAM;

    if (val)
        m68k_write_memory_32(val, (uint32_t)v);
    if (sizep)
        m68k_write_memory_32(sizep, 4);

    return 0;
}

static uint32_t socket_name(int peer)
{
    uint16_t h = peek_u16(2);
    uint32_t addr = peek_u32(4), lenp = peek_u32(8);
    struct sockaddr_in sa;
    socklen_t len = sizeof sa;
    int32_t err;
    int fd, r;

    if ((fd = socket_of(h, NULL, &err)) < 0)
        return err;

    memset(&sa, 0, sizeof sa);
    r = peer ? getpeername(fd, (struct sockaddr *)&sa, &len)
             : getsockname(fd, (struct sockaddr *)&sa, &len);
    if (r < 0)
        return mint_error(errno);

    /* The gateway's end is a socketpair, which has no address to give */
    if (sa.sin_family != AF_INET)
    {
        memset(&sa, 0, sizeof sa);
        if (peer)
        {
            sa.sin_port = htons(NET_GATEWAY_PORT);
            sa.sin_addr.s_addr = htonl(NET_GATEWAY_IP);
        }
    }

    write_sockaddr(addr, lenp, &sa);
    return 0;
}

uint32_t GEMDOS_Fgetsockname()
{
    return socket_name(0);
}

uint32_t GEMDOS_Fgetpeername()
{
    return socket_name(1);
}

uint32_t GEMDOS_Fshutdown()
{
    uint32_t how = peek_u32(4);
    int32_t err;
    int fd;

    if ((fd = socket_of(peek_u16(2), NULL, &err)) < 0)
        return err;

    if (how > 2)
        return M_EINVAL;

    return shutdown(fd, how == 0 ? SHUT_RD : how == 1 ? SHUT_WR : SHUT_RDWR) < 0
           ? mint_error(errno) : 0;
}

/*
 * Only for sockets. Every other handle answers as it did before there were
 * any, which is that there is no such call.
 */
uint32_t GEMDOS_Fcntl()
{
    uint16_t h = peek_u16(2);
    uint32_t arg = peek_u32(4);
    uint16_t cmd = peek_u16(8);
    uint32_t *sflags;
    int fd = gemdos_socket_fd(h, &sflags), n = 0;

    FUNC_TRACE_ENTER_ARGS {
        printf("    handle: %d, arg: 0x%x, cmd: 0x%x\n", h, arg, cmd);
    }

    if (fd < 0)
        return GEMDOS_EINVFN;

    switch (cmd)
    {
    case M_F_GETFL:
        return M_O_RDWR | (*sflags & M_O_NDELAY);
    case M_F_SETFL:
        *sflags = arg & M_O_NDELAY;
        return 0;
    case M_FIONREAD:
        if (ioctl(fd, FIONREAD, &n) < 0)
            return mint_error(errno);
        m68k_write_memory_32(arg, n);
        return 0;
    case M_FIONWRITE:
    {
        struct pollfd p = { fd, POLLOUT, 0 };

        m68k_write_memory_32(arg, poll(&p, 1, 0) > 0 ? 4096 : 0);
        return 0;
    }
    }

    return GEMDOS_EINVFN;
}

/*
 * The sockets among the handles, waited on; a handle that is a file is ready
 * now, which is what a file always is. The masks are a long each, handle n
 * being bit n, and a timeout of nought is no timeout.
 */
#define SELECT_HANDLES (32)

uint32_t GEMDOS_Fselect()
{
    uint16_t timeout = peek_u16(2);
    uint32_t masks[3] = { peek_u32(4), peek_u32(8), peek_u32(12) };
    static const short events[3] = { POLLIN, POLLOUT, POLLPRI };
    uint32_t want[3] = { 0, 0, 0 }, got[3] = { 0, 0, 0 };
    struct pollfd fds[SELECT_HANDLES * 3];
    int which[SELECT_HANDLES * 3], kind[SELECT_HANDLES * 3];
    int n = 0, ready = 0, i, k, r;

    for (k = 0; k < 3; k++)
        if (masks[k])
            want[k] = m68k_read_memory_32(masks[k]);

    for (k = 0; k < 3; k++)
        for (i = 0; i < SELECT_HANDLES; i++)
        {
            int fd;

            if (!(want[k] & (1u << i)))
                continue;

            fd = gemdos_socket_fd(i, NULL);
            if (fd == -2)
                return M_EBADF;
            if (fd == -1)
            {
                if (k < 2)
                    got[k] |= 1u << i;
                continue;
            }

            fds[n].fd = fd;
            fds[n].events = events[k];
            fds[n].revents = 0;
            which[n] = i;
            kind[n] = k;
            n++;
        }

    r = net_wait_many(fds, n, (got[0] | got[1]) ? 0 : (timeout ? timeout : -1));
    if (r < 0 && n > 0)
        return M_EINTR;

    for (i = 0; i < n; i++)
    {
        short ev = fds[i].revents;

        /* An error or the far end going is something a reader or a writer
         * has to hear about, which it does by trying */
        if (ev & (events[kind[i]] | POLLERR | POLLHUP))
            got[kind[i]] |= 1u << which[i];
    }

    for (k = 0; k < 3; k++)
    {
        uint32_t m = got[k];

        if (masks[k])
            m68k_write_memory_32(masks[k], m);
        for (; m; m &= m - 1)
            ready++;
    }

    return ready;
}
