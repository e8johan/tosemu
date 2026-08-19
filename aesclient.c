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
#include <poll.h>
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

    /*
     * Who the accessories are. Kept rather than asked for, because the daemon
     * says when it changes: an application puts its menu bar up once and the
     * list can change afterwards.
     */
    int16_t accessories;
    struct {
        char name[AESD_NAME_LEN + 1];
        int16_t ap_id;
    } accessory[AESD_MAX_ACCS];
} d = { -1, 0, 1, 0, 0, 0, 0, {{{0}, 0}} };

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

/* Takes down who the accessories are now, which the daemon says rather than
 * being asked */
static void accessories_are(const struct aesd_packet *p)
{
    int i;

    d.accessories = p->accessories;

    if (d.accessories > AESD_MAX_ACCS)
        d.accessories = AESD_MAX_ACCS;

    for (i = 0; i < d.accessories; i++)
    {
        memcpy(d.accessory[i].name, p->accessory[i].name, AESD_NAME_LEN);
        d.accessory[i].name[AESD_NAME_LEN] = 0;
        d.accessory[i].ap_id = p->accessory[i].ap_id;
    }
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

/*
 * Lets go of a connection that arrived by being forked.
 *
 * The daemon knows this application by the socket it said hello on, and a child
 * of fork has a copy of that socket rather than one of its own. Two processes
 * reading it would take turns getting half the messages each, and closing it
 * properly would tell the daemon the parent had gone. So it is dropped without
 * a word: the child says hello itself when it gets that far, and is a different
 * application from then on.
 */
void aes_client_forget(void)
{
    if (d.fd >= 0)
        close(d.fd);

    memset(&d, 0, sizeof d);
    d.fd = -1;
    d.apps = 1;
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

        if (p.kind == AESD_ACCESSORIES)
            accessories_are(&p);
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

/*
 * Where the scrap is, and saying where it is now.
 *
 * Kept here when there is no daemon rather than refused, because one
 * application cutting something out and pasting it back into itself is a thing
 * people do, and it works by the same two calls.
 */
static char scrap[128];

void aes_client_scrap_get(char *path, size_t size)
{
    struct aesd_packet p;

    if (d.fd < 0)
    {
        snprintf(path, size, "%s", scrap);
        return;
    }

    memset(&p, 0, sizeof p);
    p.kind = AESD_SCRAP_GET;

    if (!packet_write(&p))
    {
        daemon_gone();
        snprintf(path, size, "%s", scrap);
        return;
    }

    for (;;)
    {
        if (!packet_read(&p))
        {
            daemon_gone();
            snprintf(path, size, "%s", scrap);
            return;
        }

        if (p.kind == AESD_SCRAP)
        {
            p.path[sizeof p.path - 1] = 0;
            snprintf(path, size, "%s", p.path);
            return;
        }

        /* A message that arrived while we were asking */
        if (p.kind == AESD_DELIVER)
            aes_message_post(p.message);
    }
}

void aes_client_scrap_set(const char *path)
{
    struct aesd_packet p;

    snprintf(scrap, sizeof scrap, "%s", path ? path : "");

    if (d.fd < 0)
        return;

    memset(&p, 0, sizeof p);
    p.kind = AESD_SCRAP_SET;
    snprintf(p.path, sizeof p.path, "%s", scrap);

    if (!packet_write(&p))
        daemon_gone();
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

    if (p.kind == AESD_ACCESSORIES)
        accessories_are(&p);
}

/*
 * Saying this application is an accessory, and finding out who the others are.
 *
 * Without a daemon an accessory is an application that nothing will ever ask
 * for: there is no desk menu but its own, and it is not in it. Saying so is
 * still not an error - it runs, it simply waits for a message that will not
 * come, which is what it would do on a machine where nobody clicked it.
 */
/*
 * The desktop's notes, which one application writes and another reads.
 *
 * Kept here as well as in the daemon so that a single application still has
 * somewhere to put them - it is a buffer, and a program that puts something in
 * a buffer expects to find it there.
 */
static char notes[AESD_NOTES];
static int16_t notes_length;

void aes_client_notes_get(char *to, int size)
{
    struct aesd_packet p;

    if (d.fd < 0)
    {
        memcpy(to, notes, (size < AESD_NOTES) ? (size_t)size : AESD_NOTES);
        return;
    }

    memset(&p, 0, sizeof p);
    p.kind = AESD_NOTES_GET;

    if (!packet_write(&p))
    {
        daemon_gone();
        memcpy(to, notes, (size < AESD_NOTES) ? (size_t)size : AESD_NOTES);
        return;
    }

    for (;;)
    {
        if (!packet_read(&p))
        {
            daemon_gone();
            memcpy(to, notes, (size < AESD_NOTES) ? (size_t)size : AESD_NOTES);
            return;
        }

        if (p.kind == AESD_NOTES_ARE)
        {
            memcpy(to, p.notes,
                   (size < AESD_NOTES) ? (size_t)size : AESD_NOTES);
            return;
        }

        if (p.kind == AESD_DELIVER)
            aes_message_post(p.message);

        if (p.kind == AESD_ACCESSORIES)
            accessories_are(&p);
    }
}

void aes_client_notes_set(const char *from, int length)
{
    struct aesd_packet p;

    if (length < 0)
        length = 0;
    if (length > AESD_NOTES)
        length = AESD_NOTES;

    memcpy(notes, from, (size_t)length);
    notes_length = (int16_t)length;

    if (d.fd < 0)
        return;

    memset(&p, 0, sizeof p);
    p.kind = AESD_NOTES_SET;
    p.notes_length = notes_length;
    memcpy(p.notes, notes, sizeof p.notes);

    if (!packet_write(&p))
        daemon_gone();
}

void aes_client_accessory(const char *name)
{
    struct aesd_packet p;
    int i;

    if (d.fd < 0)
        return;

    memset(&p, 0, sizeof p);
    p.kind = AESD_ACCESSORY;
    for (i = 0; i < AESD_NAME_LEN; i++)
        p.name[i] = (name && name[i]) ? name[i] : ' ';

    if (!packet_write(&p))
        daemon_gone();
}

/*
 * Reads anything the daemon has already said, without waiting for it to say
 * more.
 *
 * The daemon tells an application who the accessories are as part of letting
 * it in, and an application that puts its menu bar up straight afterwards asks
 * before the event loop has got round to reading it. Waiting would be wrong -
 * there may be nothing to wait for - so this takes what has arrived and no
 * more.
 */
static void catch_up(void)
{
    struct pollfd waiting;

    if (d.fd < 0)
        return;

    for (;;)
    {
        struct aesd_packet p;

        waiting.fd = d.fd;
        waiting.events = POLLIN;
        waiting.revents = 0;

        if (poll(&waiting, 1, 0) <= 0)
            return;

        if (!packet_read(&p))
        {
            daemon_gone();
            return;
        }

        if (p.kind == AESD_DELIVER)
            aes_message_post(p.message);

        if (p.kind == AESD_ACCESSORIES)
            accessories_are(&p);
    }
}

int16_t aes_client_accessories(void)
{
    catch_up();

    return d.accessories;
}

const char *aes_client_accessory_name(int16_t which)
{
    if (which < 0 || which >= d.accessories)
        return 0;

    return d.accessory[which].name;
}

int16_t aes_client_accessory_owner(int16_t which)
{
    if (which < 0 || which >= d.accessories)
        return -1;

    return d.accessory[which].ap_id;
}
