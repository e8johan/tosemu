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

#include "netcache.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* webgate's own: three minutes, 48 MB in all */
#define KEEP_MS    (3L * 60 * 1000)
#define KEEP_BYTES ((size_t)48 * 1024 * 1024)

struct entry
{
    char *url;
    struct fetched *f;  /* NULL while it is being fetched */
    long long at, used;
};

static struct entry *entries;
static int count, room;
static size_t total;
static size_t most_bytes = KEEP_BYTES;
static long keep_ms = KEEP_MS;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void init(void)
{
    pthread_condattr_t a;

    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(&changed, &a);
    pthread_condattr_destroy(&a);
}

static long long now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static size_t size_of(const struct fetched *f)
{
    return strlen(f->head) + f->len;
}

struct fetched *fetched_new(const char *head, size_t head_len, uint8_t *body, size_t len,
                            const char *tls)
{
    struct fetched *f = calloc(1, sizeof *f);

    if (!f || !(f->head = malloc(head_len + 1)))
    {
        free(f);
        return NULL;
    }

    memcpy(f->head, head, head_len);
    f->head[head_len] = 0;
    f->body = body;
    f->len = len;
    strncpy(f->tls, tls ? tls : "", sizeof f->tls - 1);
    f->refs = 1;

    return f;
}

static void let_go(struct fetched *f)
{
    if (f && --f->refs == 0)
    {
        free(f->head);
        free(f->body);
        free(f);
    }
}

void fetched_release(struct fetched *f)
{
    pthread_mutex_lock(&lock);
    let_go(f);
    pthread_mutex_unlock(&lock);
}

static int find(const char *url)
{
    int i;

    for (i = 0; i < count; i++)
        if (strcmp(entries[i].url, url) == 0)
            return i;
    return -1;
}

static void drop(int i)
{
    if (entries[i].f)
    {
        total -= size_of(entries[i].f);
        let_go(entries[i].f);
    }
    free(entries[i].url);
    entries[i] = entries[--count];
}

/* What has been kept too long goes, then the least lately asked for until
 * what is left fits. One being fetched is never the one to go. */
static void sweep(void)
{
    long long now = now_ms();
    int i;

    for (i = 0; i < count;)
        if (entries[i].f && now - entries[i].at > keep_ms)
            drop(i);
        else
            i++;

    while (total > most_bytes)
    {
        int oldest = -1;

        for (i = 0; i < count; i++)
            if (entries[i].f && (oldest < 0 || entries[i].used < entries[oldest].used))
                oldest = i;
        if (oldest < 0)
            break;
        drop(oldest);
    }
}

int netcache_claim(const char *url)
{
    int r = 0;

    pthread_once(&once, init);
    pthread_mutex_lock(&lock);
    sweep();

    if (find(url) < 0)
    {
        if (count == room)
        {
            int more = room ? room * 2 : 32;
            struct entry *e = realloc(entries, more * sizeof *e);

            if (!e)
                goto done;
            entries = e;
            room = more;
        }

        entries[count].url = strdup(url);
        if (!entries[count].url)
            goto done;
        entries[count].f = NULL;
        entries[count].at = entries[count].used = now_ms();
        count++;
        r = 1;
    }

done:
    pthread_mutex_unlock(&lock);
    return r;
}

void netcache_done(const char *url, struct fetched *f)
{
    int i;

    pthread_once(&once, init);
    pthread_mutex_lock(&lock);

    i = find(url);
    if (i >= 0 && !entries[i].f)
    {
        if (f && size_of(f) <= most_bytes)
        {
            f->refs++;
            entries[i].f = f;
            entries[i].at = entries[i].used = now_ms();
            total += size_of(f);
            sweep();
        }
        else
            drop(i);
    }

    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
}

struct fetched *netcache_get(const char *url, long wait_ms)
{
    long long until = now_ms() + wait_ms;
    struct fetched *f = NULL;
    int i;

    pthread_once(&once, init);
    pthread_mutex_lock(&lock);
    sweep();

    while ((i = find(url)) >= 0)
    {
        struct timespec t;

        if (entries[i].f)
        {
            f = entries[i].f;
            f->refs++;
            entries[i].used = now_ms();
            break;
        }

        if (now_ms() >= until)
            break;

        clock_gettime(CLOCK_MONOTONIC, &t);
        t.tv_sec += 1;
        if (t.tv_sec * 1000LL + t.tv_nsec / 1000000 > until)
        {
            t.tv_sec = (time_t)(until / 1000);
            t.tv_nsec = (long)(until % 1000) * 1000000;
        }
        pthread_cond_timedwait(&changed, &lock, &t);
    }

    pthread_mutex_unlock(&lock);
    return f;
}

void netcache_limits(size_t bytes, long ms)
{
    pthread_mutex_lock(&lock);
    most_bytes = bytes ? bytes : KEEP_BYTES;
    keep_ms = ms ? ms : KEEP_MS;
    pthread_mutex_unlock(&lock);
}

void netcache_clear(void)
{
    pthread_mutex_lock(&lock);
    while (count > 0)
        drop(0);
    pthread_mutex_unlock(&lock);
}
