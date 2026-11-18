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
 * The gateway's pictures kept: claimed once, waited for while they are
 * being fetched, and let go of when they are old or there are too many.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "netcache.h"

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

static void pause_ms(long ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };

    nanosleep(&t, NULL);
}

static struct fetched *answer(size_t len)
{
    static const char head[] = "HTTP/1.1 200 OK\r\n\r\n";
    uint8_t *body = calloc(1, len ? len : 1);

    return fetched_new(head, sizeof head - 1, body, len, "TLS 1.3, X");
}

/* A fetch that takes a while, on a thread of its own, as a worker's is */
static void *slow_fetch(void *arg)
{
    struct fetched *f = answer(10);

    (void)arg;
    pause_ms(200);
    netcache_done("https://slow:443/a", f);
    fetched_release(f);
    return NULL;
}

static void *failing_fetch(void *arg)
{
    (void)arg;
    pause_ms(100);
    netcache_done("https://fails:443/a", NULL);
    return NULL;
}

int main(void)
{
    struct fetched *f, *g;
    pthread_t thread;

    check(netcache_get("https://a:443/x", 0) == NULL, 1, "nothing is kept to start with");
    check(netcache_claim("https://a:443/x"), 1, "an address is claimed by the first to fetch it");
    check(netcache_claim("https://a:443/x"), 0, "and not by the second");

    f = answer(100);
    netcache_done("https://a:443/x", f);
    g = netcache_get("https://a:443/x", 0);
    check(g == f, 1, "what was fetched is there to have");
    check(g && strcmp(g->tls, "TLS 1.3, X") == 0, 1, "with how it was reached");
    fetched_release(g);
    fetched_release(f);
    g = netcache_get("https://a:443/x", 0);
    check(g != NULL && g->len == 100, 1, "and kept after its fetcher let go of it");
    fetched_release(g);

    netcache_claim("https://slow:443/a");
    pthread_create(&thread, NULL, slow_fetch, NULL);
    check(netcache_get("https://slow:443/a", 0) == NULL, 1, "one being fetched is not there yet");
    g = netcache_get("https://slow:443/a", 5000);
    check(g != NULL && g->len == 10, 1, "but is waited for");
    fetched_release(g);
    pthread_join(thread, NULL);

    netcache_claim("https://fails:443/a");
    pthread_create(&thread, NULL, failing_fetch, NULL);
    check(netcache_get("https://fails:443/a", 5000) == NULL, 1,
          "and one that fails ends the wait with nothing");
    pthread_join(thread, NULL);
    check(netcache_claim("https://fails:443/a"), 1, "so that it can be fetched again");
    netcache_done("https://fails:443/a", NULL);

    netcache_limits(0, 100);
    pause_ms(150);
    check(netcache_get("https://a:443/x", 0) == NULL, 1, "what is old is let go of");

    netcache_limits(1000, 0);
    netcache_claim("https://a:443/1");
    f = answer(600);
    netcache_done("https://a:443/1", f);
    fetched_release(f);
    pause_ms(5);
    netcache_claim("https://a:443/2");
    f = answer(600);
    netcache_done("https://a:443/2", f);
    fetched_release(f);
    g = netcache_get("https://a:443/1", 0);
    check(g == NULL, 1, "and when there is too much, what was asked for least lately");
    fetched_release(g);
    g = netcache_get("https://a:443/2", 0);
    check(g != NULL, 1, "keeping the newer");
    fetched_release(g);

    netcache_claim("https://a:443/huge");
    f = answer(5000);
    netcache_done("https://a:443/huge", f);
    fetched_release(f);
    check(netcache_get("https://a:443/huge", 0) == NULL, 1, "one larger than all there is room for is not kept");

    netcache_limits(0, 0);
    netcache_clear();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
