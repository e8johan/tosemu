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
 * What the built-in gateway makes of a request and of an answer, without a
 * network: the first line read into where it goes, and the two heads
 * rewritten so that the connection closes after one answer.
 */

#include <stdio.h>
#include <string.h>

#include "netgate.h"

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

static void check_str(const char *got, const char *want, const char *name)
{
    n++;

    if (strcmp(got, want) == 0)
        printf("ok %d - %s\n", n, name);
    else
    {
        fails++;
        printf("not ok %d - %s\n# got:  [%s]\n# want: [%s]\n", n, name, got, want);
    }
}

static void check_request_lines(void)
{
    struct netgate_target t;
    char longline[NETGATE_TARGET_MAX + 64];

    check(netgate_parse_request_line("GET https://example.org/a?b=1 HTTP/1.1\r\n", &t),
          NETGATE_PROXY, "an https address in proxy form");
    check(t.tls, 1, "is secure");
    check_str(t.method, "GET", "its method");
    check_str(t.host, "example.org", "its host");
    check(t.port, 443, "on the port https has");
    check_str(t.target, "/a?b=1", "and its path with the query");

    check(netgate_parse_request_line("POST http://10.0.0.2:8080 HTTP/1.0", &t),
          NETGATE_PROXY, "an http address with a port and no path");
    check(t.tls, 0, "is not secure");
    check(t.port, 8080, "on the port it named");
    check_str(t.target, "/", "and asks for the root");

    netgate_parse_request_line("GET http://example.org?q HTTP/1.1", &t);
    check_str(t.target, "/?q", "a query with no path is the root's");

    check(netgate_parse_request_line("GET / HTTP/1.1\r\n", &t), NETGATE_ORIGIN,
          "a request of the gateway itself");

    check(netgate_parse_request_line("GET http://[::1]/ HTTP/1.1", &t), NETGATE_BAD,
          "an IPv6 literal is refused");
    check(netgate_parse_request_line("GET http://user@host/ HTTP/1.1", &t), NETGATE_BAD,
          "and so is user information");
    check(netgate_parse_request_line("GET http://host:0/ HTTP/1.1", &t), NETGATE_BAD,
          "and port nought");
    check(netgate_parse_request_line("GET http://host:65536/ HTTP/1.1", &t), NETGATE_BAD,
          "and a port past the last");
    check(netgate_parse_request_line("GET http://host:/ HTTP/1.1", &t), NETGATE_BAD,
          "and a colon with no port");
    check(netgate_parse_request_line("GET ftp://host/ HTTP/1.1", &t), NETGATE_BAD,
          "and a scheme it does not speak");
    check(netgate_parse_request_line("GET http://host/ HTTP/2", &t), NETGATE_BAD,
          "and a version it does not speak");
    check(netgate_parse_request_line("get http://host/ HTTP/1.1", &t), NETGATE_BAD,
          "and a method that is not one");

    memset(longline, 'a', sizeof longline);
    memcpy(longline, "GET http://host/", 16);
    strcpy(longline + sizeof longline - 10, " HTTP/1.1");
    check(netgate_parse_request_line(longline, &t), NETGATE_BAD,
          "and a path too long to keep");
}

static void check_lengths(void)
{
    check(netgate_content_length("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 12\r\n\r\n"),
          12, "a body's length");
    check(netgate_content_length("GET / HTTP/1.1\r\nHost: a\r\n\r\n"), -1,
          "no body");
    check(netgate_content_length("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"), -2,
          "a chunked body is not passed on");
    check(netgate_content_length("POST / HTTP/1.1\r\nContent-Length: 1x\r\n\r\n"), -2,
          "and nor is a length that is not one");
}

static void check_heads(void)
{
    struct netgate_target t;
    char out[1024];
    const char *req = "GET https://example.org/x HTTP/1.1\r\n"
                      "Host: example.org\r\n"
                      "Connection: keep-alive\r\n"
                      "X-Gateway-Picture: fit=320\r\n"
                      "Accept: */*\r\n\r\n";
    const char *bare = "GET http://example.org:81/ HTTP/1.1\r\nAccept: */*\r\n\r\n";
    const char *ans = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/html\r\n"
                      "Keep-Alive: timeout=5\r\n"
                      "Connection: keep-alive\r\n"
                      "X-Gateway-TLS: forged\r\n"
                      "Content-Length: 5\r\n\r\n";

    netgate_parse_request_line(req, &t);
    check(netgate_request_head(req, &t, out, sizeof out) > 0, 1, "the request goes upstream");
    check_str(out, "GET /x HTTP/1.1\r\nHost: example.org\r\nAccept: */*\r\n"
                   "Connection: close\r\n\r\n",
              "in origin form, without what was for the gateway, closing after");

    netgate_parse_request_line(bare, &t);
    netgate_request_head(bare, &t, out, sizeof out);
    check_str(out, "GET / HTTP/1.1\r\nAccept: */*\r\nHost: example.org:81\r\n"
                   "Connection: close\r\n\r\n",
              "a request without a Host is given one, with the port");

    check(netgate_request_head(req, &t, out, 20), -1, "a head that does not fit is refused");

    check(netgate_response_head(ans, "TLS 1.3, TLS_AES_128_GCM_SHA256", NULL, NULL,
                                out, sizeof out) > 0, 1,
          "the answer comes back");
    check_str(out, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 5\r\n"
                   "X-Gateway-TLS: TLS 1.3, TLS_AES_128_GCM_SHA256\r\n"
                   "Connection: close\r\n\r\n",
              "saying how it was reached, and that the connection ends with it");

    netgate_response_head(ans, NULL, NULL, NULL, out, sizeof out);
    check(strstr(out, "X-Gateway-TLS") == NULL, 1,
          "a plain answer says nothing about TLS, and a server's own word is dropped");

    {
        static const char *const drop[] = { "Content-Type", "Content-Length", NULL };

        netgate_response_head(ans, NULL, drop, "Content-Type: image/jpeg\r\nContent-Length: 9\r\n",
                              out, sizeof out);
        check_str(out, "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: 9\r\n"
                       "Connection: close\r\n\r\n",
                  "a picture made over has its own type and length in place of the server's");
    }
}

static void check_reading_answers(void)
{
    char value[32];
    uint8_t body[64];
    const char *chunked = "4\r\nWiki\r\n5;ext=1\r\npedia\r\n0\r\n\r\n";

    check(netgate_status("HTTP/1.1 404 Not Found\r\n"), 404, "an answer's status");
    check(netgate_status("SPDY 200\r\n"), -1, "and none from what is not one");

    check(netgate_header("GET / HTTP/1.1\r\nX-Gateway-Picture:  fit=608 \r\n\r\n",
                         "X-Gateway-Picture", value, sizeof value), 1, "a header is found");
    check_str(value, "fit=608", "and its value trimmed");
    check(netgate_header("GET / HTTP/1.1\r\nHost: a\r\n\r\n", "X-Gateway-Picture",
                         value, sizeof value), 0, "and one that is not there is not");

    strcpy((char *)body, chunked);
    check(netgate_dechunk(body, strlen(chunked)), 9, "a chunked body comes apart");
    check(memcmp(body, "Wikipedia", 9), 0, "into what it carried");

    strcpy((char *)body, "4\r\nWiki\r\n5\r\nped");
    check(netgate_dechunk(body, strlen((char *)body)), -1, "and one cut short does not");
}

static void check_prefetch_lists(void)
{
    struct netgate_prefetch lines[2];
    const char *referer, *accept;
    char body[] = "Referer: https://example.org/page\r\n"
                  "Accept: image/jpeg\n"
                  "https://example.org/a.jpg\tfit=608;pens=16;pal=1a2b3c4d\tsid=x\n"
                  "nonsense without a tab\n"
                  "https://example.org/b.png\tfit=608\n"
                  "https://example.org/c.png\tfit=608\n";

    check(netgate_prefetch_lines(body, &referer, &accept, lines, 2), 2,
          "a list of pictures to make ahead is read, as many as there is room for");
    check_str(referer, "https://example.org/page", "with the page they are for");
    check_str(accept, "image/jpeg", "and what it takes");
    check_str(lines[0].url, "https://example.org/a.jpg", "each picture's address");
    check_str(lines[0].ask, "fit=608;pens=16;pal=1a2b3c4d", "how it is asked for");
    check_str(lines[0].cookie, "sid=x", "and its cookies");
    check_str(lines[1].url, "https://example.org/b.png", "a line that is not one passed over");
    check_str(lines[1].cookie, "", "and no cookies where there are none");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    check_request_lines();
    check_lengths();
    check_heads();
    check_reading_answers();
    check_prefetch_lists();

    printf("1..%d\n", n);

    return fails ? 1 : 0;
}
