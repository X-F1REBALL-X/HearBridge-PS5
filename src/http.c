/* Web control: one self-contained page plus a tiny JSON API, served by a
 * single background thread. All state goes through hb_ctl (mutex); the
 * stream loop applies the requests. Developed by X-F1REBALL-X. */
#include "http.h"
#include "webpage.h"
#include "diag.h"
#include "rate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- request handling (pure) ----------------------------------------- */

static int query_int(const char *q, const char *key, int *val)
{
    size_t kl = strlen(key);
    while (q && *q) {
        if (!strncmp(q, key, kl) && q[kl] == '=') {
            const char *p = q + kl + 1;
            int neg = 0, v = 0, any = 0;
            if (*p == '-') { neg = 1; p++; }
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; any = 1; if (v > 100000) break; }
            if (!any) return 0;
            *val = neg ? -v : v;
            return 1;
        }
        q = strchr(q, '&');
        if (q) q++;
    }
    return 0;
}

static void json_esc(char *o, size_t max, const char *s)
{
    size_t n = 0;
    for (; *s && n + 7 < max; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') { o[n++] = '\\'; o[n++] = (char)ch; }
        else if (ch < 0x20) n += (size_t)snprintf(o + n, max - n, "\\u%04x", ch);
        else o[n++] = (char)ch;
    }
    o[n] = 0;
}

static int status_json(hb_ctl *c, char *o, int max)
{
    char dev[140], st[70], url[140], det[200];
    json_esc(dev, sizeof dev, c->device);
    json_esc(st, sizeof st, c->state);
    json_esc(url, sizeof url, c->url);
    json_esc(det, sizeof det, c->detail);
    return snprintf(o, (size_t)max,
        "{\"version\":\"%s\",\"connected\":%d,\"detail\":\"%s\",\"state\":\"%s\",\"device\":\"%s\",\"url\":\"%s\","
        "\"gain_pct\":%d,\"muted\":%d,\"tone\":%d,\"paused\":%d,"
        "\"headset_volume\":%d,\"avrcp\":{\"connected\":%d,\"absolute_volume\":%d,"
        "\"notifications\":%d},\"pkts\":%ld,\"frames\":%ld,\"empty_reads\":%ld,"
        "\"peak\":%.3f,\"out_peak\":%.3f,\"sample_rate\":%d,\"bitpool\":%d,"
        "\"backlog\":%d,\"bitpool_min\":%d,\"bitpool_max\":%d,\"per_packet\":%d,"
        "\"dropped\":%ld,\"uptime_s\":%ld,\"stream_s\":%ld,\"stable\":%d,\"queue_ms\":%d}",
        c->version, !strcmp(c->state, "streaming"), det, st, dev, url, c->gain_pct, c->muted, c->tone, c->paused,
        c->hs_volume, c->avrcp & 1, (c->avrcp >> 1) & 1, (c->avrcp >> 2) & 1,
        c->pkts, c->frames, c->empty_reads, c->peak_milli / 1000.0,
        c->out_peak_milli / 1000.0, c->sample_rate, c->bitpool, c->backlog,
        c->bitpool_lo, c->bitpool_hi, c->per_packet, c->dropped, ctl_uptime_s(c), c->uptime_s,
        c->stable, c->stable ? HB_QUEUE_STABLE_MS : HB_QUEUE_LOW_MS);
}

static int respond(char *out, int max, int code, const char *ctype,
                   const char *body, int blen)
{
    const char *reason = code == 200 ? "OK" : code == 404 ? "Not Found" :
                         code == 405 ? "Method Not Allowed" :
                         code == 500 ? "Internal Server Error" : "Bad Request";
    int n = snprintf(out, (size_t)max,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        code, reason, ctype, blen);
    if (n < 0 || n + blen > max) return 0;
    memcpy(out + n, body, (size_t)blen);
    return n + blen;
}

int http_handle(hb_ctl *c, const char *req, int reqlen, char *out, int max)
{
    char method[8], path[128], *q;
    char body[1024];
    int i = 0, j = 0, v, bl, is_api;

    while (i < reqlen && req[i] != ' ' && j < (int)sizeof method - 1) method[j++] = req[i++];
    method[j] = 0;
    if (i >= reqlen || req[i] != ' ') return respond(out, max, 400, "text/plain", "bad request\n", 12);
    i++;
    j = 0;
    while (i < reqlen && req[i] != ' ' && req[i] != '\r' && j < (int)sizeof path - 1) path[j++] = req[i++];
    path[j] = 0;
    if (strcmp(method, "GET") && strcmp(method, "POST"))
        return respond(out, max, 405, "text/plain", "GET or POST\n", 12);
    q = strchr(path, '?');
    if (q) *q++ = 0;

    if (!strcmp(path, "/") || !strcmp(path, "/index.html"))
        return respond(out, max, 200, "text/html; charset=utf-8", HB_WEBPAGE,
                       (int)sizeof HB_WEBPAGE - 1);

    is_api = !strncmp(path, "/api/", 5);
    if (!is_api) return respond(out, max, 404, "text/plain", "not found\n", 10);

    if (!strcmp(path, "/api/diag")) {
        /* Plain-text diagnostics report (see diag.h), also in diag.txt. */
        static char dt[60000];
        int n = diag_text(dt, sizeof dt);
        if (n <= 0) {
            strcpy(dt, "no diagnostics collected yet\n");
            n = (int)strlen(dt);
        }
        return respond(out, max, 200, "text/plain; charset=utf-8", dt, n);
    }
    if (!strcmp(path, "/api/devices") || !strcmp(path, "/api/saved")) {
        /* devices.json from the scan, or saved.json (paired list, no keys). */
        static char dj[8192];
        char dp[96];
        FILE *f;
        int n = 0;
        CTL_LOCK(c);
        snprintf(dp, sizeof dp, "%s", path[5] == 'd' ? c->devices_path : c->saved_path);
        CTL_UNLOCK(c);
        f = dp[0] ? fopen(dp, "r") : NULL;
        if (f) {
            n = (int)fread(dj, 1, sizeof dj - 1, f);
            fclose(f);
        }
        if (n <= 0) {
            strcpy(dj, "{\"version\":1,\"devices\":[]}");
            n = (int)strlen(dj);
        }
        return respond(out, max, 200, "application/json", dj, n);
    }
    if (!strcmp(path, "/api/select") || !strcmp(path, "/api/forget") ||
        !strcmp(path, "/api/scan") || !strcmp(path, "/api/reconnect")) {
        /* Commands for the stream loop, through select.txt:
         *   select?addr=XX:..|index=N   forget?addr=XX:..   scan   reconnect */
        char line[40], sp[96];
        int forget = path[5] == 'f';
        const char *a = q ? strstr(q, "addr=") : NULL;
        FILE *f;
        line[0] = 0;
        if (path[5] == 's' && path[6] == 'c') { strcpy(line, "scan"); a = NULL; q = NULL; }
        else if (path[5] == 'r') { strcpy(line, "reconnect"); a = NULL; q = NULL; }
        if (a) {
            int k;
            a += 5;
            for (k = 0; k < 17; k++) {
                char ch = a[k];
                int hex = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') ||
                          (ch >= 'a' && ch <= 'f');
                if ((k % 3 == 2) ? ch != ':' : !hex) break;
                line[k] = ch;
            }
            line[17] = 0;
            if (k != 17 || (a[17] && a[17] != '&')) line[0] = 0;
        } else if (!forget && query_int(q, "index", &v) && v >= 0 && v < 64) {
            snprintf(line, sizeof line, "%d", v);
        }
        if (forget && line[0]) {
            char t[40];
            snprintf(t, sizeof t, "forget %s", line);
            strcpy(line, t);
        }
        if (!line[0])
            return respond(out, max, 400, "application/json", "{\"error\":\"bad parameter\"}", 25);
        CTL_LOCK(c);
        snprintf(sp, sizeof sp, "%s", c->select_path);
        CTL_UNLOCK(c);
        {
            char tp[104];
            snprintf(tp, sizeof tp, "%s.tmp", sp);
            f = sp[0] ? fopen(tp, "w") : NULL;
            if (!f) return respond(out, max, 500, "application/json", "{\"error\":\"write\"}", 17);
            fprintf(f, "%s\n", line);
            fclose(f);
            if (rename(tp, sp) != 0)
                return respond(out, max, 500, "application/json", "{\"error\":\"write\"}", 17);
        }
        CTL_LOCK(c);
        c->cmd_seq++;                  /* the stream loop sees a new command */
        CTL_UNLOCK(c);
        if (!forget) {                 /* any pick/scan/reconnect leaves "paused" */
            CTL_LOCK(c);
            c->paused = 0;
            CTL_UNLOCK(c);
        }
        return respond(out, max, 200, "application/json", "{\"ok\":1}", 8);
    }

    CTL_LOCK(c);
    if (!strcmp(path, "/api/status")) {
        /* read only */
    } else if (!strcmp(path, "/api/volume")) {
        if (!query_int(q, "pct", &v)) goto bad;
        if (v < 0) v = 0;
        if (v > HB_GAIN_MAX_PCT) v = HB_GAIN_MAX_PCT;
        c->gain_pct = v;
        c->gain_dirty = 1;
    } else if (!strcmp(path, "/api/headset")) {
        if (query_int(q, "vol", &v)) { }
        else if (query_int(q, "pct", &v)) v = (v * 127 + 50) / 100;
        else goto bad;
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        c->req_hs_volume = v;
        c->hs_volume = v;
    } else if (!strcmp(path, "/api/latency")) {
        /* stable=1: ~1 s media queue; stable=0: low latency (~200 ms). */
        if (!query_int(q, "stable", &v)) goto bad;
        c->stable = v != 0;
        c->stable_dirty = 1;
    } else if (!strcmp(path, "/api/mute")) {
        c->muted = query_int(q, "on", &v) ? (v != 0) : !c->muted;
    } else if (!strcmp(path, "/api/tone")) {
        c->tone = query_int(q, "on", &v) ? (v != 0) : !c->tone;
    } else if (!strcmp(path, "/api/connect")) {
        c->req_connect = 1;
        c->paused = 0;
    } else if (!strcmp(path, "/api/disconnect")) {
        c->req_disconnect = 1;
        c->paused = 1;
    } else if (!strcmp(path, "/api/stop")) {
        c->req_stop = 1;
    } else {
        CTL_UNLOCK(c);
        return respond(out, max, 404, "application/json", "{\"error\":\"unknown\"}", 19);
    }
    bl = status_json(c, body, (int)sizeof body);
    CTL_UNLOCK(c);
    if (bl < 0 || bl >= (int)sizeof body) bl = 0;
    return respond(out, max, 200, "application/json", body, bl);
bad:
    CTL_UNLOCK(c);
    return respond(out, max, 400, "application/json", "{\"error\":\"bad parameter\"}", 25);
}

/* ---- server thread (console only) ------------------------------------ */
#ifndef HB_HTTP_HOST_TEST
#include "log.h"
#include "stop.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int g_srv = -1;
static volatile int g_quit;
static pthread_t g_thr;
static hb_ctl *g_c;

static void console_ip(char *ip, size_t n)
{
    struct ifaddrs *ifa, *p;
    snprintf(ip, n, "<console-ip>");
    if (getifaddrs(&ifa) != 0) return;
    for (p = ifa; p; p = p->ifa_next) {
        struct sockaddr_in *sa;
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        sa = (struct sockaddr_in *)(void *)p->ifa_addr;
        if ((ntohl(sa->sin_addr.s_addr) >> 24) == 127) continue;
        inet_ntop(AF_INET, &sa->sin_addr, ip, (socklen_t)n);
        break;
    }
    freeifaddrs(ifa);
}

static void serve_one(int fd)
{
    static char req[4096], out[65536];
    int got = 0, n;
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    while (got < (int)sizeof req - 1) {
        n = (int)recv(fd, req + got, sizeof req - 1 - (size_t)got, 0);
        if (n <= 0) break;
        got += n;
        req[got] = 0;
        if (strstr(req, "\r\n\r\n")) break;     /* headers done; no body used */
    }
    if (got <= 0) return;
    n = http_handle(g_c, req, got, out, (int)sizeof out);
    {
        int stop;
        CTL_LOCK(g_c);
        stop = g_c->req_stop;
        CTL_UNLOCK(g_c);
        if (stop) hb_stop_request();   /* main loop sees it within ~1 ms */
    }
    {
        int off = 0;
        while (off < n) {
            int w = (int)send(fd, out + off, (size_t)(n - off), 0);
            if (w <= 0) break;
            off += w;
        }
    }
}

static void *srv_main(void *arg)
{
    (void)arg;
    while (!g_quit) {
        fd_set rs;
        struct timeval tv = { 0, 500000 };
        int fd;
        FD_ZERO(&rs);
        FD_SET(g_srv, &rs);
        if (select(g_srv + 1, &rs, NULL, NULL, &tv) <= 0) continue;
        fd = accept(g_srv, NULL, NULL);
        if (fd < 0) continue;
        serve_one(fd);
        close(fd);
    }
    return NULL;
}

int http_start(hb_ctl *c, char *url, int url_max)
{
    int port, one = 1;
    char ip[48];
    g_c = c;
    for (port = HB_HTTP_PORT; port < HB_HTTP_PORT + 6; port++) {
        struct sockaddr_in a;
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return 0;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((unsigned short)port);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(s, (struct sockaddr *)&a, sizeof a) == 0 && listen(s, 4) == 0) {
            g_srv = s;
            break;
        }
        log_line("http: port %d unavailable (errno %d)", port, errno);
        close(s);
    }
    if (g_srv < 0) return 0;
    g_quit = 0;
    if (pthread_create(&g_thr, NULL, srv_main, NULL) != 0) {
        close(g_srv);
        g_srv = -1;
        return 0;
    }
    console_ip(ip, sizeof ip);
    snprintf(url, (size_t)url_max, "http://%s:%d", ip, port);
    return port;
}

void http_stop(void)
{
    if (g_srv < 0) return;
    g_quit = 1;
    pthread_join(g_thr, NULL);
    close(g_srv);
    g_srv = -1;
}
#endif
