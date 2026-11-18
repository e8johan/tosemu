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

#include "net.h"

#include "gemdosmem_p.h"
#include "interrupt.h"
#include "netgate.h"
#include "netpic.h"
#include "settings.h"
#include "sting.h"
#include "tossystem.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* How long a wait looks before it services the machine again */
#define SLICE_MS (20)

int net_enabled(void)
{
    return !setting_flag("TOSEMU_NO_NETWORK");
}

int net_socket(int type)
{
    int fd = socket(AF_INET, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    return fd < 0 ? -errno : fd;
}

/*
 * The gateway, on the far end of a socketpair put where the socket was. dup2
 * keeps the descriptor's number, so whatever the program was given for it
 * goes on naming the same connection.
 */
static int connect_gateway(int fd)
{
    int pair[2];
    int flags;

    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) < 0)
        return -errno;

    if (dup2(pair[0], fd) < 0)
    {
        int err = errno;

        close(pair[0]);
        close(pair[1]);
        return -err;
    }
    close(pair[0]);

    /* dup2 leaves close-on-exec off, and the status flags travel with the
     * description rather than the number */
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    /* What was free when the program first came, read here, on the
     * emulator's own thread, and handed to the gateway's: the memory is the
     * machine's */
    if (netgate_serve(pair[1], netpic_budget(mem_free_first_asked())) < 0)
    {
        close(pair[1]);
        return -ECONNREFUSED;
    }

    return 0;
}

int net_connect(int fd, uint32_t ip, uint16_t port)
{
    struct sockaddr_in sa;

    if (ip == NET_GATEWAY_IP)
    {
        int type = 0;
        socklen_t len = sizeof type;

        getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len);
        if (type == SOCK_STREAM && port == NET_GATEWAY_PORT && netgate_enabled())
            return connect_gateway(fd);

        return -ECONNREFUSED;
    }

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(ip);

    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0)
        return -errno;

    return 0;
}

int net_resolve(const char *name, uint32_t *ip)
{
    struct addrinfo hints, *res;

    if (strcasecmp(name, NET_GATEWAY_NAME) == 0 && netgate_enabled())
    {
        *ip = NET_GATEWAY_IP;
        return 0;
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(name, NULL, &hints, &res) != 0 || res == NULL)
        return -1;

    *ip = ntohl(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);

    return 0;
}

static long long now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int net_wait_many(struct pollfd *fds, int n, long timeout_ms)
{
    long long end = now_ms() + (timeout_ms < 0 ? 0 : timeout_ms);

    for (;;)
    {
        long left = timeout_ms < 0 ? SLICE_MS : (long)(end - now_ms());
        int r;

        sting_flush();

        r = poll(fds, n, left < 0 ? 0 : (left > SLICE_MS ? SLICE_MS : (int)left));
        if (r > 0)
            return r;
        if (r < 0 && errno != EINTR)
            return -1;

        interrupt_service();

        if (execution_halted())
            return -1;
        if (timeout_ms >= 0 && now_ms() >= end)
            return 0;
    }
}

int net_wait(int fd, short events, long timeout_ms)
{
    struct pollfd p;
    int r;

    p.fd = fd;
    p.events = events;
    p.revents = 0;

    r = net_wait_many(&p, 1, timeout_ms);

    return r > 0 ? p.revents : r;
}

int net_socket_error(int fd)
{
    int err = 0;
    socklen_t len = sizeof err;

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
        return -errno;

    return -err;
}
