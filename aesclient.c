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
 * Talking to the daemon.
 *
 * Everything here has an answer for when there is no daemon, and that answer
 * is not an error: one application on its own is the ordinary way to run a
 * program, and it is the way every test runs. What the daemon adds is the
 * things two applications have to agree about, and with one application there
 * is nothing to agree.
 *
 * So this file has two shapes of function. Ask something about the world and
 * it either asks the daemon or answers for a world with one application in it.
 * Which of the two happened is not visible to the caller and must not be: the
 * AES above this does not have a single-application case and a
 * several-application case, it has one case.
 */

#include "aesclient.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "aes_p.h"
#include "aesproto.h"
#include "gem_p.h"

static struct {
    int fd;                     /* -1 when there is no daemon */
    int16_t ap_id;
    int16_t apps;
    int16_t width, height, planes;
} d = { -1, 0, 1, 0, 0, 0 };

/* Where the daemon's socket is, which a test overrides so that it can run one
 * of its own without disturbing the one the person is using */
const char *aes_client_socket_path(char *buffer, size_t size)
{
    const char *said = getenv("TOSEMU_AESD");
    const char *dir;

    if (said && *said)
    {
        snprintf(buffer, size, "%s", said);
        return buffer;
    }

    dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || !*dir)
        dir = "/tmp";

    snprintf(buffer, size, "%s/%s", dir, AESD_SOCKET_NAME);

    return buffer;
}

/* Reads exactly one packet, or says the daemon has gone */
static int packet_read(struct aesd_packet *p)
{
    size_t got = 0;

    while (got < sizeof *p)
    {
        ssize_t n = read(d.fd, (char *)p + got, sizeof *p - got);

        if (n > 0)
        {
            got += (size_t)n;
            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        return 0;
    }

    return 1;
}

static int packet_write(const struct aesd_packet *p)
{
    size_t sent = 0;

    if (d.fd < 0)
        return 0;

    while (sent < sizeof *p)
    {
        ssize_t n = write(d.fd, (const char *)p + sent, sizeof *p - sent);

        if (n > 0)
        {
            sent += (size_t)n;
            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        return 0;
    }

    return 1;
}

/* The daemon going away is not an error to report at every call afterwards.
 * It is the world becoming what it is when there was never one. */
static void daemon_gone(void)
{
    if (d.fd >= 0)
        close(d.fd);

    d.fd = -1;
}

int aes_client_open(void)
{
    struct sockaddr_un where;
    char path[sizeof where.sun_path];
    int fd;

    if (d.fd >= 0)
        return 1;

    aes_client_socket_path(path, sizeof path);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return 0;

    memset(&where, 0, sizeof where);
    where.sun_family = AF_UNIX;
    snprintf(where.sun_path, sizeof where.sun_path, "%s", path);

    if (connect(fd, (struct sockaddr *)&where, sizeof where) < 0)
    {
        /* Nobody there, which is the ordinary case */
        close(fd);
        return 0;
    }

    d.fd = fd;

    return 1;
}

void aes_client_close(void)
{
    daemon_gone();
}

int aes_client_connected(void)
{
    return d.fd >= 0;
}

int aes_client_fd(void)
{
    return d.fd;
}

int16_t aes_client_hello(const char *name)
{
    struct aesd_packet p;
    int i;

    /* The screen this build makes when nobody says otherwise */
    d.ap_id = 0;
    d.apps = 1;
    gem_default_screen(&d.width, &d.height, &d.planes);

    if (!aes_client_open())
        return d.ap_id;

    memset(&p, 0, sizeof p);
    p.kind = AESD_HELLO;
    for (i = 0; i < AESD_NAME_LEN; i++)
        p.name[i] = (name && name[i]) ? name[i] : ' ';

    if (!packet_write(&p) || !packet_read(&p) || p.kind != AESD_WELCOME)
    {
        daemon_gone();
        return d.ap_id;
    }

    d.ap_id = p.ap_id;
    d.apps = p.apps;
    d.width = p.screen_width;
    d.height = p.screen_height;
    d.planes = p.screen_planes;

    return d.ap_id;
}

int16_t aes_client_apps(void)
{
    return d.apps;
}

void aes_client_screen(int16_t *width, int16_t *height, int16_t *planes)
{
    if (!d.width)
        gem_default_screen(&d.width, &d.height, &d.planes);

    *width = d.width;
    *height = d.height;
    *planes = d.planes;
}

int16_t aes_client_find(const char *name)
{
    struct aesd_packet p;
    int i;

    if (d.fd < 0)
        return -1;      /* There is one application, and it is asking */

    memset(&p, 0, sizeof p);
    p.kind = AESD_FIND;
    for (i = 0; i < AESD_NAME_LEN; i++)
        p.name[i] = (name && name[i]) ? name[i] : ' ';

    if (!packet_write(&p))
    {
        daemon_gone();
        return -1;
    }

    /*
     * Anything that arrives before the answer is a message for us, which the
     * daemon is entitled to send at any time. It goes in the queue and the
     * waiting continues.
     */
    for (;;)
    {
        if (!packet_read(&p))
        {
            daemon_gone();
            return -1;
        }

        if (p.kind == AESD_FOUND)
            return p.ap_id;

        if (p.kind == AESD_DELIVER)
            aes_message_post(p.message);
    }
}

int aes_client_send(int16_t to, const int16_t *message)
{
    struct aesd_packet p;
    int i;

    if (d.fd < 0)
        return 0;

    memset(&p, 0, sizeof p);
    p.kind = AESD_SEND;
    p.ap_id = to;
    for (i = 0; i < 8; i++)
        p.message[i] = message[i];

    if (!packet_write(&p))
    {
        daemon_gone();
        return 0;
    }

    return 1;
}

void aes_client_pump(void)
{
    struct aesd_packet p;

    if (d.fd < 0)
        return;

    if (!packet_read(&p))
    {
        daemon_gone();
        return;
    }

    if (p.kind == AESD_DELIVER)
        aes_message_post(p.message);
}
