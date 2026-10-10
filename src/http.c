/* Web control: one self-contained page plus a tiny JSON API, served by a
 * single background thread. All state goes through hb_ctl (mutex); the
 * stream loop applies the requests. Developed by X-F1REBALL-X. */
#include "http.h"
#include "webpage.h"
#include "font_woff2.h"
#include "diag.h"
#include "rate.h"
#include "btchip.h"
#include "backup.h"
#include "avrcp.h"
#include "version.h"
#include "gameprof.h"
#ifndef HB_HTTP_HOST_TEST
#include "hcidbg.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- request handling (pure) ----------------------------------------- */

static int hexd(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

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

/* Value of header `name` (case-insensitive) into v; 1 if present. */
static int header_value(const char *req, int reqlen, const char *name, char *v, size_t vmax)
{
    size_t nl = strlen(name);
    int i = 0;
    /* skip the request line */
    while (i < reqlen && req[i] != '\n') i++;
    while (++i < reqlen) {
        int j = 0;
        if (req[i] == '\r' || req[i] == '\n') break;        /* end of headers */
        while (i + j < reqlen && (size_t)j < nl &&
               (req[i + j] | 0x20) == (name[j] | 0x20)) j++;
        if ((size_t)j == nl && i + j < reqlen && req[i + j] == ':') {
            size_t n = 0;
            i += j + 1;
            while (i < reqlen && (req[i] == ' ' || req[i] == '\t')) i++;
            while (i < reqlen && req[i] != '\r' && req[i] != '\n' && n + 1 < vmax) v[n++] = req[i++];
            v[n] = 0;
            return 1;
        }
        while (i < reqlen && req[i] != '\n') i++;
    }
    return 0;
}

/* Same length, compared without an early exit. */
static int token_ok(const char *got, const char *want)
{
    size_t i, n = strlen(want);
    unsigned d = 0;
    if (!n || strlen(got) != n) return 0;
    for (i = 0; i < n; i++) d |= (unsigned char)(got[i] ^ want[i]);
    return d == 0;
}

static int is_write_path(const char *path)
{
    static const char *const w[] = {
        "/api/select", "/api/forget", "/api/scan", "/api/reconnect", "/api/volume",
        "/api/headset", "/api/mute", "/api/tone", "/api/connect", "/api/disconnect",
        "/api/stop", "/api/reset", "/api/latency", "/api/codec", "/api/eq", "/api/clean", "/api/night",
        "/api/hci", "/api/game", "/api/backup", "/api/restore", "/api/keyvol",
    };
    size_t i;
    for (i = 0; i < sizeof w / sizeof w[0]; i++)
        if (!strcmp(path, w[i])) return 1;
    return 0;
}

static int status_json(hb_ctl *c, char *o, int max)
{
    char dev[140], st[70], url[140], det[200], why[40], ev[2400], cid[2][8], gname[100], gfrom[70];
    static char gl[32 * 600];
    const char *cven = btchip_vendor(c->chip_vid);
    int ei, en, chip_ok = c->chip_vid >= 0 && c->chip_vid <= 0xffff &&
                          c->chip_pid >= 0 && c->chip_pid <= 0xffff;
    json_esc(dev, sizeof dev, c->device);
    json_esc(st, sizeof st, c->state);
    json_esc(url, sizeof url, c->url);
    json_esc(det, sizeof det, c->detail);
    json_esc(why, sizeof why, c->why);
    json_esc(gname, sizeof gname, c->game_name);
    json_esc(gfrom, sizeof gfrom, c->game_from);
    {
        int gi, gn = 1;
        gl[0] = '[';
        for (gi = 0; gi < c->games_n && gi < 32; gi++) {
            char nm[100];
            int hk;
            json_esc(nm, sizeof nm, c->games_name[gi]);
            if (!hb_game_id_ok(c->games_id[gi])) continue;
            gn += snprintf(gl + gn, sizeof gl - (size_t)gn, "%s{\"id\":\"%s\",\"name\":\"%s\",\"hs\":[",
                           gn > 1 ? "," : "", c->games_id[gi], nm);
            for (hk = 0; hk < c->games_hs_n[gi] && hk < 4 && gn < (int)sizeof gl - 2; hk++) {
                char hn[70];
                const unsigned char *a = c->games_hs[gi][hk];
                json_esc(hn, sizeof hn, c->games_hsname[gi][hk]);
                gn += snprintf(gl + gn, sizeof gl - (size_t)gn,
                               "%s{\"a\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"n\":\"%s\",\"cur\":%d}",
                               hk ? "," : "", a[0], a[1], a[2], a[3], a[4], a[5], hn, c->games_hs_cur[gi][hk] ? 1 : 0);
            }
            if (gn < (int)sizeof gl - 2) gn += snprintf(gl + gn, sizeof gl - (size_t)gn, "]}");
            if (gn >= (int)sizeof gl - 2) { gn = 1; break; }
        }
        gl[gn] = ']';
        gl[gn + 1] = 0;
    }
    ev[0] = '[';
    en = 1;
    for (ei = 0; ei < c->event_n && ei < HB_EVENT_N; ei++) {
        char one[HB_EVENT_LEN * 2];
        json_esc(one, sizeof one, c->events[ei]);
        en += snprintf(ev + en, sizeof ev - (size_t)en, "%s\"%s\"", ei ? "," : "", one);
        if (en < 1 || en >= (int)sizeof ev - 2) break;
    }
    if (en > 0 && en < (int)sizeof ev) ev[en++] = ']';
    ev[en < (int)sizeof ev ? en : (int)sizeof ev - 1] = 0;
    cid[0][0] = cid[1][0] = 0;
    if (chip_ok) {
        snprintf(cid[0], sizeof cid[0], "%04x", c->chip_vid);
        snprintf(cid[1], sizeof cid[1], "%04x", c->chip_pid);
    }
    return snprintf(o, (size_t)max,
        "{\"version\":\"%s\",\"token\":\"%s\",\"connected\":%d,\"detail\":\"%s\",\"why\":\"%s\",\"state\":\"%s\",\"device\":\"%s\",\"url\":\"%s\","
        "\"gain_pct\":%d,\"muted\":%d,\"tone\":%d,\"paused\":%d,"
        "\"headset_volume\":%d,\"avrcp\":{\"connected\":%d,\"absolute_volume\":%d,"
        "\"notifications\":%d,\"sink_volume\":%d},\"pkts\":%ld,\"frames\":%ld,\"empty_reads\":%ld,"
        "\"peak\":%.3f,\"out_peak\":%.3f,\"sample_rate\":%d,\"bitpool\":%d,"
        "\"backlog\":%d,\"bitpool_min\":%d,\"bitpool_max\":%d,\"per_packet\":%d,"
        "\"dropped\":%ld,\"uptime_s\":%ld,\"stream_s\":%ld,\"stable\":%d,\"queue_ms\":%d,\"latency\":{\"target_ms\":%d,\"estimate_ms\":%d,\"capture_ms\":%d,\"packet_ms\":%d,\"queue_ms\":%d,\"radio_ms\":%d,\"sink_ms\":%d,\"sink_reported\":%d},\"codec\":\"%s\",\"codec_pref\":%d,\"codec_avail\":%d,"
        "\"eq\":{\"on\":%d,\"db\":[%d,%d,%d,%d,%d]},\"xq_low\":%d,"
        "\"chip\":{\"vid\":\"%s\",\"pid\":\"%s\",\"vendor\":\"%s\",\"mediatek\":%d,\"profile\":\"%s\"},"
        "\"lat_extra\":{\"backoff_ms\":%d,\"normal_ms\":%d,\"auto\":%d,\"auto_ms\":%d},"
        "\"battery\":{\"status\":\"%s\",\"level\":%d,\"pct\":%d,\"none\":%d},\"hs_moves\":%d,"
        "\"link\":{\"score\":%d,\"rssi\":%d,\"lq\":%d,\"drops_min\":%d},"
        "\"night\":{\"on\":%d,\"db10\":%d},\"batt_alert\":{\"level\":%d,\"seq\":%u},\"rest_watch\":%d,\"key_vol\":%d,"
        "\"game\":{\"avail\":%d,\"id\":\"%s\",\"name\":\"%s\",\"profile\":%d,\"active\":%d,\"exact\":%d,\"from\":\"%s\",\"dirty\":%d,\"saved\":%s},\"events\":%s}",
        c->version, c->token, !strcmp(c->state, "streaming"), det, why, st, dev, url, c->gain_pct, c->muted, c->tone, c->paused,
        c->hs_volume, c->avrcp & 1, (c->avrcp >> 1) & 1, (c->avrcp >> 2) & 1, (c->avrcp >> 3) & 1,
        c->pkts, c->frames, c->empty_reads, c->peak_milli / 1000.0,
        c->out_peak_milli / 1000.0, c->sample_rate, c->bitpool, c->backlog,
        c->bitpool_lo, c->bitpool_hi, c->per_packet, c->dropped, ctl_uptime_s(c), c->uptime_s,
        c->latency_ms >= 500, c->latency_ms,
        c->latency_ms, c->lat_total, c->lat_capture, c->lat_packet, c->lat_queue, c->lat_radio,
        c->lat_sink, c->lat_sink_reported, c->codec,
        c->codec_pref, c->codec_avail,
        c->eq_on, c->eq_db[0], c->eq_db[1], c->eq_db[2], c->eq_db[3], c->eq_db[4],
        c->xq_low, cid[0], cid[1], chip_ok && cven ? cven : "",
        chip_ok && btchip_is_mediatek(c->chip_vid),
        chip_ok ? btchip_profile_name(btchip_profile(c->chip_vid, btchip_get_override())) : "",
        c->lat_backoff_ms, c->lat_normal_ms, c->latency_auto != 0, c->lat_auto_ms,
        avrcp_battery_key(c->battery), avrcp_battery_level(c->battery), c->batt_pct, c->batt_none, c->hs_moves,
        c->link_score, c->link_rssi == 127 ? 0 : c->link_rssi, c->link_lq, c->drops_min,
        c->night, c->night_db10, c->batt_alert, c->batt_alert_seq, c->rest_watch, c->key_vol,
        c->game_avail, c->game_id, gname, c->game_profile, c->game_active, c->game_exact, gfrom, c->game_dirty, gl, ev);
}

static int respond(char *out, int max, int code, const char *ctype,
                   const char *body, int blen)
{
    const char *reason = code == 200 ? "OK" : code == 404 ? "Not Found" :
                         code == 405 ? "Method Not Allowed" : code == 403 ? "Forbidden" :
                         code == 409 ? "Conflict" :
                         code == 500 ? "Internal Server Error" : "Bad Request";
    int n = snprintf(out, (size_t)max,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        code, reason, ctype, blen);
    if (n < 0 || n + blen > max) return 0;
    memcpy(out + n, body, (size_t)blen);
    return n + blen;
}

static long wall_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long)ts.tv_sec;
}

/* Value of a short word parameter (letters, digits, - _ .) into v. */
static int query_word(const char *q, const char *key, char *v, int vmax)
{
    size_t kl = strlen(key);
    while (q && *q) {
        if (!strncmp(q, key, kl) && q[kl] == '=') {
            const char *p = q + kl + 1;
            int n = 0;
            while (*p && *p != '&' && n < vmax - 1) {
                char ch = *p++;
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                      ch == '-' || ch == '_' || ch == '.')) return 0;
                v[n++] = ch;
            }
            v[n] = 0;
            return n > 0;
        }
        q = strchr(q, '&');
        if (q) q++;
    }
    return 0;
}

/* Backup folder for where=usb|internal; 0 (and "no USB") if none. */
static int backup_dir(hb_ctl *c, const char *where, char *dir, int max)
{
    char mnt[32];
    CTL_LOCK(c);
    snprintf(mnt, sizeof mnt, "%s", c->mnt_root);
    CTL_UNLOCK(c);
    if (!strcmp(where, "internal")) {
        if (!strcmp(mnt, "/mnt")) snprintf(dir, (size_t)max, "%s", HB_BACKUP_INTERNAL);
        else snprintf(dir, (size_t)max, "%s/internal-backup", mnt);   /* tests */
        return 1;
    }
    if (!strcmp(where, "usb")) return hb_backup_find_usb(mnt, dir, max);
    return 0;
}

/*  GET  /api/backups?where=usb|internal          list (newest first)
 *  POST /api/backup?where=usb|internal|download  save (download: JSON back)
 *  POST /api/restore?where=usb|internal&name=..  or where=upload + JSON body */
static int backup_api(hb_ctl *c, const char *path, const char *q, const char *req, int reqlen,
                      char *out, int max)
{
    static char big[HB_BACKUP_MAX + 512];
    char where[16], dir[160], sd[64], name[48];
    int n;
    if (!query_word(q, "where", where, sizeof where))
        return respond(out, max, 400, "application/json", "{\"error\":\"bad parameter\"}", 25);
    CTL_LOCK(c);
    snprintf(sd, sizeof sd, "%s", c->state_dir);
    CTL_UNLOCK(c);
    if (!strcmp(path, "/api/backup") && !strcmp(where, "download")) {
        n = hb_backup_make(sd, HEARBRIDGE_VERSION, wall_s(), big, (int)sizeof big);
        if (n <= 0) return respond(out, max, 500, "application/json", "{\"error\":\"read\"}", 16);
        CTL_LOCK(c);
        ctl_event_locked(c, "Backup downloaded");
        CTL_UNLOCK(c);
        return respond(out, max, 200, "application/json", big, n);
    }
    if (!strcmp(path, "/api/restore") && !strcmp(where, "upload")) {
        const char *body = NULL;
        int i, bl;
        for (i = 0; i + 3 < reqlen; i++)
            if (req[i] == '\r' && req[i + 1] == '\n' && req[i + 2] == '\r' && req[i + 3] == '\n') {
                body = req + i + 4;
                break;
            }
        bl = body ? reqlen - (int)(body - req) : 0;
        if (bl <= 0 || bl > HB_BACKUP_MAX)
            return respond(out, max, 400, "application/json", "{\"error\":\"no file\"}", 19);
        memcpy(big, body, (size_t)bl);
        big[bl] = 0;
        n = hb_backup_restore_text(sd, big);
        dir[0] = 0;
    } else {
        if (!backup_dir(c, where, dir, sizeof dir)) {
            int l = snprintf(big, sizeof big, "{\"where\":\"%s\",\"avail\":0,\"files\":[]}", where);
            return respond(out, max, !strcmp(path, "/api/backups") ? 200 : 409, "application/json", big, l);
        }
        if (!strcmp(path, "/api/backups")) {
            char names[HB_BACKUP_KEEP][HB_BACKUP_NAME];
            int k, l, cnt = hb_backup_list(dir, names, HB_BACKUP_KEEP);
            l = snprintf(big, sizeof big, "{\"where\":\"%s\",\"avail\":1,\"dir\":\"%s\",\"files\":[", where, dir);
            for (k = 0; k < cnt && l > 0 && l < (int)sizeof big - 120; k++) {
                char when[24];
                hb_backup_when(names[k], when, sizeof when);
                l += snprintf(big + l, sizeof big - (size_t)l, "%s{\"name\":\"%s\",\"when\":\"%s\"}",
                              k ? "," : "", names[k], when);
            }
            l += snprintf(big + l, sizeof big - (size_t)l, "]}");
            return respond(out, max, 200, "application/json", big, l);
        }
        if (!strcmp(path, "/api/backup")) {
            int l;
            if (!hb_backup_save(sd, dir, HEARBRIDGE_VERSION, wall_s(), name, sizeof name))
                return respond(out, max, 500, "application/json", "{\"error\":\"write\"}", 17);
            CTL_LOCK(c);
            ctl_event_locked(c, !strcmp(where, "usb") ? "Backup saved to USB" : "Backup saved on the console");
            CTL_UNLOCK(c);
            l = snprintf(big, sizeof big, "{\"ok\":1,\"name\":\"%s\",\"dir\":\"%s\"}", name, dir);
            return respond(out, max, 200, "application/json", big, l);
        }
        if (!query_word(q, "name", name, sizeof name) || !hb_backup_name_ok(name))
            return respond(out, max, 400, "application/json", "{\"error\":\"bad parameter\"}", 25);
        n = hb_backup_restore(sd, dir, name);
    }
    if (n <= 0)
        return respond(out, max, 400, "application/json", "{\"error\":\"not a HearBridge backup\"}", 35);
    CTL_LOCK(c);
    c->req_reload = 1;
    ctl_event_locked(c, "Settings restored");
    CTL_UNLOCK(c);
    {
        int l = snprintf(big, sizeof big, "{\"ok\":1,\"files\":%d}", n);
        return respond(out, max, 200, "application/json", big, l);
    }
}

int http_gameicon_id(const char *req, int reqlen, char *id, int idmax)
{
    static const char pre[] = "GET /api/gameicon?id=";
    int n = (int)sizeof pre - 1, k = 0;
    if (idmax > 0) id[0] = 0;
    if (reqlen < n || memcmp(req, pre, (size_t)n)) return 0;
    while (n < reqlen && k < idmax - 1 && req[n] != ' ' && req[n] != '&' && req[n] != '\r') id[k++] = req[n++];
    id[k] = 0;
    return hb_game_id_ok(id);
}

int http_handle(hb_ctl *c, const char *req, int reqlen, char *out, int max)
{
    char method[8], path[128], *q;
    static char body[32768];   /* status with the saved games list (per headset) */
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

    if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
        /* The page carries this run's token (same length as the slot). */
        int n = respond(out, max, 200, "text/html; charset=utf-8", HB_WEBPAGE,
                        (int)sizeof HB_WEBPAGE - 1);
        int i, sl = (int)sizeof HB_TOKEN_SLOT - 1;
        for (i = 0; n > 0 && i + sl <= n; i++)
            if (out[i] == 'H' && !memcmp(out + i, HB_TOKEN_SLOT, (size_t)sl)) {
                memcpy(out + i, c->token, HB_TOKEN_LEN);
                break;
            }
        return n;
    }

    if (!strcmp(path, "/font.woff2")) {
        /* The page font (Inter, Latin), kept by the browser for a week. */
        int n = snprintf(out, (size_t)max, "HTTP/1.1 200 OK\r\nContent-Type: font/woff2\r\nContent-Length: %d\r\n"
                         "Cache-Control: public, max-age=604800\r\nConnection: close\r\n\r\n",
                         (int)sizeof hb_font_woff2);
        if (n < 0 || n + (int)sizeof hb_font_woff2 > max) return 0;
        memcpy(out + n, hb_font_woff2, sizeof hb_font_woff2);
        return n + (int)sizeof hb_font_woff2;
    }

    is_api = !strncmp(path, "/api/", 5);
    if (!is_api) return respond(out, max, 404, "text/plain", "not found\n", 10);

    if (is_write_path(path)) {
        char tok[HB_TOKEN_LEN + 8];
        if (strcmp(method, "POST"))
            return respond(out, max, 405, "application/json", "{\"error\":\"use POST\"}", 20);
        if (!header_value(req, reqlen, "X-HB-Token", tok, sizeof tok) || !token_ok(tok, c->token))
            return respond(out, max, 403, "application/json", "{\"error\":\"token\"}", 17);
    }

#ifndef HB_HTTP_HOST_TEST
    if (!hcidbg_enabled() && (!strcmp(path, "/api/hcilog") || !strcmp(path, "/api/hci")))
        return respond(out, max, 404, "application/json", "{\"error\":\"unknown\"}", 19);
    if (!strcmp(path, "/api/hcilog")) {
        /* HCI trace: "seq ms CMD op bytes" / "seq ms EVT bytes"; ?since=seq */
        static char ht[120000];
        int since = 0, n;
        (void)query_int(q, "since", &since);
        n = hcidbg_text(since > 0 ? (unsigned)since : 0, ht, (int)sizeof ht);
        return respond(out, max, 200, "text/plain; charset=utf-8", ht, n);
    }
    if (!strcmp(path, "/api/hci")) {
        /* Raw HCI command (debug): op=XXXX&p=HEX, sent from the stream loop. */
        return hcidbg_queue(q) ? respond(out, max, 200, "application/json", "{\"ok\":1}", 8)
                               : respond(out, max, 409, "application/json", "{\"error\":\"busy or bad\"}", 24);
    }
#endif
    if (!strcmp(path, "/api/gameicon"))   /* a good id is streamed by the server (big file) */
        return respond(out, max, 404, "text/plain", "no icon\n", 8);
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
        if (path[5] == 's' && path[6] == 'c') {
            /* "scanu": the Scan / Add headset button (stops a stream for it).
             * A scan nobody pressed (old pages sent one on every load) is
             * ignored: an inquiry holds the radio the DualSense shares, and
             * it started background pages again (#29). */
            if (!(query_int(q, "user", &v) && v == 1))
                return respond(out, max, 200, "application/json", "{\"ok\":1}", 8);
            strcpy(line, "scanu");
            a = NULL;
            q = NULL;
        }
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
        if (!strcmp(line, "scan") && sp[0]) {
            /* A refresh starts a scan, but it must not erase a reconnect,
             * a Scan press or a device pick that has not been read yet. */
            FILE *oldf = fopen(sp, "r");
            char prev[40];
            prev[0] = 0;
            if (oldf) {
                if (!fgets(prev, sizeof prev, oldf)) prev[0] = 0;
                fclose(oldf);
                prev[strcspn(prev, "\r\n")] = 0;
                if (!strcmp(prev, "reconnect") || !strcmp(prev, "scanu") ||
                    (strchr(prev, ':') && strncmp(prev, "forget ", 7)))
                    return respond(out, max, 200, "application/json", "{\"ok\":1}", 8);
            }
        }
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
        {
            char evl[64];
            if (!strncmp(line, "scan", 4)) snprintf(evl, sizeof evl, "Scan started");
            else if (!strcmp(line, "reconnect")) snprintf(evl, sizeof evl, "Reconnecting");
            else if (forget) snprintf(evl, sizeof evl, "Forgetting a headset");
            else if (strchr(line, ':')) snprintf(evl, sizeof evl, "Connecting");
            else snprintf(evl, sizeof evl, "Connecting");
            CTL_LOCK(c);
            c->cmd_seq++;                  /* the stream loop sees a new command */
            ctl_event_locked(c, evl);
            CTL_UNLOCK(c);
        }
        if (!forget && strcmp(line, "scan")) {   /* a pick, Scan or Reconnect leaves "paused" */
            CTL_LOCK(c);
            c->paused = 0;
            c->req_disconnect = 0;
            CTL_UNLOCK(c);
        }
        return respond(out, max, 200, "application/json", "{\"ok\":1}", 8);
    }

    if (!strcmp(path, "/api/backups") || !strcmp(path, "/api/backup") || !strcmp(path, "/api/restore"))
        return backup_api(c, path, q, req, reqlen, out, max);

    CTL_LOCK(c);
    if (!strcmp(path, "/api/status")) {
        c->status_polls++;   /* page open: main may re-query the headset volume */
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
        /* ms=40..200: media queue target by hand (adaptive off), saved per
         * headset. auto=1: adaptive (lowest drop-free buffer, learned per
         * headset), auto=0: back to the slider value.
         * stable=0|1 (older pages): both land on 200 ms now. */
        if (query_int(q, "auto", &v)) {
            c->latency_auto = v != 0;
            c->prefs_dirty = 1;
        } else {
            if (query_int(q, "ms", &v)) { }
            else if (query_int(q, "stable", &v)) v = v ? HB_QUEUE_STABLE_MS : HB_QUEUE_LOW_MS;
            else goto bad;
            c->latency_ms = hb_latency_clamp(v);
            c->latency_auto = 0;
            c->prefs_dirty = 1;
        }
    } else if (!strcmp(path, "/api/codec")) {
        /* mode=0 auto, 1 SBC, 2 SBC HQ, 3 SBC-XQ (hsprefs.h); the stream
         * loop switches the headset to it. HQ / XQ only when the connected
         * headset's capabilities allow them (codec_avail). */
        if (!query_int(q, "mode", &v) || v < 0 || v > 3) goto bad;
        if (v >= 2 && !(c->codec_avail & (1 << v))) {
            CTL_UNLOCK(c);
            return respond(out, max, 409, "application/json",
                           "{\"error\":\"not supported by this headset\"}", 41);
        }
        c->codec_pref = v;
        c->prefs_dirty = 1;
    } else if (!strcmp(path, "/api/keyvol")) {
        /* on=0|1: earbud next / previous track keys change the volume
         * (all headsets, saved in the keyvol file). */
        if (!query_int(q, "on", &v)) goto bad;
        c->key_vol = v != 0;
        c->key_vol_dirty = 1;
    } else if (!strcmp(path, "/api/night")) {
        /* on=0|1: night mode compressor, saved per headset. */
        if (!query_int(q, "on", &v)) goto bad;
        c->night = v != 0;
        c->prefs_dirty = 1;
    } else if (!strcmp(path, "/api/clean")) {
        /* EQ off and flat, software gain back to the default, buffer 200 ms.
         * Saved for this headset (prefs) and the gain file. */
        int k;
        c->eq_on = 0;
        c->night = 0;
        for (k = 0; k < 5; k++) c->eq_db[k] = 0;
        c->eq_seq++;
        c->gain_pct = HB_GAIN_DEFAULT_PCT;
        c->gain_dirty = 1;
        c->latency_ms = HB_QUEUE_LOW_MS;
        c->latency_auto = 1;
        c->prefs_dirty = 1;
    } else if (!strcmp(path, "/api/eq")) {
        /* on=0|1 and/or b0..b4=-12..12 (dB); saved per headset. */
        static const char *const bk[5] = { "b0", "b1", "b2", "b3", "b4" };
        int k, any = 0;
        if (query_int(q, "on", &v)) { c->eq_on = v != 0; any = 1; }
        for (k = 0; k < 5; k++)
            if (query_int(q, bk[k], &v)) {
                c->eq_db[k] = v < -12 ? -12 : v > 12 ? 12 : v;
                any = 1;
            }
        if (!any) goto bad;
        c->eq_seq++;
        c->prefs_dirty = 1;
    } else if (!strcmp(path, "/api/game")) {
        /* do=1 save the live EQ / boost / night / headset volume for the
         * running game on this headset (Save / Update Game Profile), do=2
         * forget this headset's profile of it. The stream loop does it. */
        char gid[16];
        if (query_int(q, "do", &v) && v == 3) {
            /* do=3&id=PPSA01234: forget that saved game (Games list) */
            if (!query_word(q, "id", gid, sizeof gid) || !hb_game_id_ok(gid)) goto bad;
            snprintf(c->req_game_drop, sizeof c->req_game_drop, "%s", gid);
            if (!strcmp(gid, c->game_id)) { c->game_profile = 0; c->game_active = 0; }
            goto done_game;
        }
        if (query_int(q, "do", &v) && (v == 4 || v == 5)) {
            /* do=4&id=..&hs=AA:..: forget that headset's profile of the game;
             * do=5: use that headset's profile now (the game is running). */
            const char *h = q ? strstr(q, "hs=") : NULL;
            unsigned char a[6];
            int k;
            if (!query_word(q, "id", gid, sizeof gid) || !hb_game_id_ok(gid) || !h) goto bad;
            h += 3;
            for (k = 0; k < 6; k++) {
                int hi = hexd(h[k * 3]), lo = hexd(h[k * 3 + 1]);
                if (hi < 0 || lo < 0 || (k < 5 && h[k * 3 + 2] != ':')) goto bad;
                a[k] = (unsigned char)(hi * 16 + lo);
            }
            if (h[17] && h[17] != '&') goto bad;
            if (v == 5 && strcmp(gid, c->game_id)) goto bad;
            snprintf(c->req_game_hs, sizeof c->req_game_hs, "%s", gid);
            memcpy(c->req_game_hs_addr, a, 6);
            c->req_game_hs_do = v == 4 ? 1 : 2;
            goto done_game;
        }
        if (!query_int(q, "do", &v) || v < 1 || v > 2 || !c->game_id[0]) goto bad;
        c->req_game = v;
        if (v == 1) { c->game_profile = 1; c->game_active = 1; c->game_exact = 1; c->game_dirty = 0; c->game_from[0] = 0; }
        else { c->game_profile = 0; c->game_active = 0; }
    done_game:;
    } else if (!strcmp(path, "/api/mute")) {
        c->muted = query_int(q, "on", &v) ? (v != 0) : !c->muted;
    } else if (!strcmp(path, "/api/tone")) {
        c->tone = query_int(q, "on", &v) ? (v != 0) : !c->tone;
    } else if (!strcmp(path, "/api/connect")) {
        ctl_event_locked(c, "Connecting");
        c->req_connect = 1;
        c->req_disconnect = 0;         /* a Disconnect pressed while idle is over */
        c->paused = 0;
    } else if (!strcmp(path, "/api/disconnect")) {
        ctl_event_locked(c, "Disconnecting");
        c->req_disconnect = 1;
        c->paused = 1;
    } else if (!strcmp(path, "/api/stop")) {
        c->req_stop = 1;
    } else if (!strcmp(path, "/api/reset")) {
        /* Our page and our headset ACL only. Not an HCI reset. */
        c->req_reset = 1;
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
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
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

static int send_all(int fd, const char *b, int n)
{
    int off = 0;
    while (off < n) {
        int w = (int)send(fd, b + off, (size_t)(n - off), 0);
        if (w <= 0) return -1;
        off += w;
    }
    return 0;
}

/* Game icon from the console's appmeta, streamed (icon0.png can be bigger
 * than the reply buffer). The browser keeps it for a week. 0 = not found. */
static int send_gameicon(int fd, const char *id)
{
    static char buf[16384];
    char path[96], hdr[200];
    struct stat st;
    int i, f = -1, n;
    for (i = 0; f < 0 && hb_game_icon_path(id, i, path, sizeof path); i++)
        if (stat(path, &st) == 0 && st.st_size > 0 && st.st_size < 8 * 1024 * 1024)
            f = open(path, O_RDONLY);
    if (f < 0) return 0;
    n = snprintf(hdr, sizeof hdr, "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: %ld\r\n"
                 "Cache-Control: public, max-age=604800\r\nConnection: close\r\n\r\n", (long)st.st_size);
    if (send_all(fd, hdr, n) == 0) {
        long left = (long)st.st_size;
        while (left > 0 && (n = (int)read(f, buf, sizeof buf)) > 0) {
            if (send_all(fd, buf, n)) break;
            left -= n;
        }
    }
    close(f);
    return 1;
}

static void serve_one(int fd)
{
    static char req[HB_BACKUP_MAX + 4096], out[196608];   /* the page (~75 KB with all languages) */
    int got = 0, n, need = -1;
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    while (got < (int)sizeof req - 1) {
        n = (int)recv(fd, req + got, sizeof req - 1 - (size_t)got, 0);
        if (n <= 0) break;
        got += n;
        req[got] = 0;
        if (need < 0) {
            char *hend = strstr(req, "\r\n\r\n");
            if (hend) {
                /* headers done; only an uploaded backup carries a body */
                char cl[16];
                int len = 0, k;
                if (header_value(req, got, "Content-Length", cl, sizeof cl))
                    for (k = 0; cl[k] >= '0' && cl[k] <= '9' && len < 1000000; k++) len = len * 10 + (cl[k] - '0');
                need = (int)(hend - req) + 4 + (len > HB_BACKUP_MAX ? 0 : len);
            }
        }
        if (need >= 0 && got >= need) break;
    }
    if (got <= 0) return;
    {
        char gid[16];
        if (http_gameicon_id(req, got, gid, sizeof gid) && send_gameicon(fd, gid)) return;
    }
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
        int wait;
        /* The usual port: a copy that is closing may hold it a moment
         * longer (#25), so wait for it before moving to the next one. */
        for (wait = 0; ; wait += 250) {
            struct sockaddr_in a;
            int s = socket(AF_INET, SOCK_STREAM, 0), e;
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
            e = errno;
            close(s);
            if (port != HB_HTTP_PORT || e != EADDRINUSE || wait >= 3000) {
                log_line("http: port %d unavailable (errno %d)", port, e);
                break;
            }
            usleep(250 * 1000);
        }
        if (g_srv >= 0) break;
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
