/*
 * Minimal single-threaded HTTP/1.1 server for signalling.
 *
 *   GET    /                    built-in player page
 *   POST   /api/offer           -> 201, body = SDP offer (application/sdp),
 *                                  Location: /api/session/<id>
 *   POST   /api/session/<id>    body = SDP answer -> 204
 *   DELETE /api/session/<id>    -> 204
 *   GET    /api/status          -> {"viewers":N}
 *
 * The camera offers and the browser answers because libpeer only
 * implements that direction. Connection: close on every response.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "http.h"
#include "log.h"
#include "session.h"

#define MAX_REQUEST (16 * 1024)

extern const char akwebrtc_index_html[];

static int send_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static void respond(int fd, int code, const char *reason, const char *ctype,
                    const char *extra_hdr, const char *body, size_t body_len)
{
    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Access-Control-Expose-Headers: Location\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n"
                     "%s%s%s%s\r\n",
                     code, reason, body_len,
                     ctype ? "Content-Type: " : "", ctype ? ctype : "", ctype ? "\r\n" : "",
                     extra_hdr ? extra_hdr : "");
    if (n < 0 || (size_t)n >= sizeof(hdr))
        return;
    if (send_all(fd, hdr, (size_t)n) == 0 && body_len)
        send_all(fd, body, body_len);
}

static void respond_text(int fd, int code, const char *reason, const char *msg)
{
    respond(fd, code, reason, "text/plain", NULL, msg, strlen(msg));
}

/* Read the full request (headers + Content-Length body), 5 s timeout. */
static int read_request(int fd, char *buf, size_t cap, size_t *hdr_len, size_t *total)
{
    struct timeval tv = {5, 0};
    size_t len = 0;
    long clen = -1;

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    *hdr_len = 0;
    for (;;) {
        if (len >= cap - 1)
            return -1;
        ssize_t n = recv(fd, buf + len, cap - 1 - len, 0);
        if (n <= 0)
            return -1;
        len += (size_t)n;
        buf[len] = '\0';
        if (!*hdr_len) {
            char *e = strstr(buf, "\r\n\r\n");
            if (!e)
                continue;
            *hdr_len = (size_t)(e - buf) + 4;
            clen = 0;
            for (char *l = strstr(buf, "\r\n"); l && l < e; l = strstr(l + 2, "\r\n")) {
                if (!strncasecmp(l + 2, "Content-Length:", 15)) {
                    clen = strtol(l + 17, NULL, 10);
                    break;
                }
            }
            if (clen < 0 || (size_t)clen > cap - 1 - *hdr_len)
                return -1;
        }
        if (len >= *hdr_len + (size_t)clen) {
            *total = *hdr_len + (size_t)clen;
            buf[*total] = '\0';
            return 0;
        }
    }
}

/* libpeer copies each SDP line into a 256-byte stack buffer; refuse
 * anything that would overflow it. */
static int sdp_lines_ok(const char *sdp)
{
    const char *p = sdp;
    while (*p) {
        const char *e = strstr(p, "\r\n");
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n >= 256)
            return 0;
        if (!e)
            break;
        p = e + 2;
    }
    return 1;
}

static void handle(int fd)
{
    char *req = malloc(MAX_REQUEST);
    size_t hdr_len, total;
    char method[8], path[128];

    if (!req)
        return;
    if (read_request(fd, req, MAX_REQUEST, &hdr_len, &total) != 0 ||
        sscanf(req, "%7s %127s", method, path) != 2) {
        respond_text(fd, 400, "Bad Request", "bad request\n");
        free(req);
        return;
    }
    char *body = req + hdr_len;
    size_t body_len = total - hdr_len;

    if (!strcmp(method, "OPTIONS")) {
        respond(fd, 204, "No Content", NULL, NULL, NULL, 0);
    } else if (!strcmp(method, "GET") && (!strcmp(path, "/") || !strcmp(path, "/index.html"))) {
        respond(fd, 200, "OK", "text/html; charset=utf-8", NULL,
                akwebrtc_index_html, strlen(akwebrtc_index_html));
    } else if (!strcmp(method, "GET") && !strcmp(path, "/api/status")) {
        char js[64];
        int n = snprintf(js, sizeof(js), "{\"viewers\":%d}\n", session_count());
        respond(fd, 200, "OK", "application/json", NULL, js, (size_t)n);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/offer")) {
        char *offer = NULL;
        session_reap();
        int id = session_create(&offer);
        if (id < 0) {
            respond_text(fd, 503, "Service Unavailable", "viewer limit reached\n");
        } else {
            char loc[64];
            snprintf(loc, sizeof(loc), "Location: /api/session/%d\r\n", id);
            respond(fd, 201, "Created", "application/sdp", loc, offer, strlen(offer));
            free(offer);
        }
    } else if (!strncmp(path, "/api/session/", 13)) {
        int id = atoi(path + 13);
        if (!strcmp(method, "POST")) {
            if (body_len == 0 || !strstr(body, "v=0") || !sdp_lines_ok(body))
                respond_text(fd, 400, "Bad Request", "expected SDP answer\n");
            else if (session_set_answer(id, body) != 0)
                respond_text(fd, 404, "Not Found", "no such session\n");
            else
                respond(fd, 204, "No Content", NULL, NULL, NULL, 0);
        } else if (!strcmp(method, "DELETE")) {
            if (session_close(id) != 0)
                respond_text(fd, 404, "Not Found", "no such session\n");
            else
                respond(fd, 204, "No Content", NULL, NULL, NULL, 0);
        } else {
            respond_text(fd, 405, "Method Not Allowed", "method not allowed\n");
        }
    } else {
        respond_text(fd, 404, "Not Found", "not found\n");
    }
    free(req);
}

int http_serve(int port, volatile int *running)
{
    int one = 1;
    struct sockaddr_in addr;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0)
        return -1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(lfd, 8) < 0) {
        LOG_E("cannot listen on port %d: %s", port, strerror(errno));
        close(lfd);
        return -1;
    }
    LOG_I("http: listening on port %d", port);

    while (*running) {
        struct pollfd p = {lfd, POLLIN, 0};
        int r = poll(&p, 1, 1000);
        session_reap();
        if (r <= 0)
            continue;
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0)
            continue;
        handle(cfd);
        close(cfd);
    }
    close(lfd);
    return 0;
}
