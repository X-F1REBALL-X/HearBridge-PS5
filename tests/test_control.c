/* Host tests: HTTP handler, SDP server records, AVRCP absolute volume,
 * soft limiter and gain file parsing. Prints the status JSON to argv[1]
 * for an external JSON parse check. */
#include "ctl.h"
#include "gain.h"
#include "http.h"
#include "diag.h"
#include "avrcp.h"
#include "sdp_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void log_line(const char *fmt, ...) { (void)fmt; }

static int fails;
#define CHECK(c, what) do { if (c) printf("ok   %s\n", what); \
    else { printf("FAIL %s\n", what); fails++; } } while (0)

static char out[196608];

static int get(hb_ctl *c, const char *path)
{
    char req[256];
    snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: ps5\r\n\r\n", path);
    return http_handle(c, req, (int)strlen(req), out, (int)sizeof out);
}

/* State-changing request as the page sends it: POST + X-HB-Token. */
static int post(hb_ctl *c, const char *path)
{
    char req[320];
    snprintf(req, sizeof req, "POST %s HTTP/1.1\r\nHost: ps5\r\nContent-Length: 0\r\n"
             "x-hb-token: %s\r\n\r\n", path, c->token);
    return http_handle(c, req, (int)strlen(req), out, (int)sizeof out);
}

static const unsigned char *find(const unsigned char *h, int hn, const unsigned char *n, int nn)
{
    int i;
    for (i = 0; i + nn <= hn; i++) if (!memcmp(h + i, n, (size_t)nn)) return h + i;
    return NULL;
}

int main(int argc, char **argv)
{
    hb_ctl c;
    int n;

    /* ---- HTTP ---- */
    ctl_init(&c, "1.0.9");
    strcpy(c.url, "http://10.0.0.5:8090");
    strcpy(c.device, "WF-\"1000\"XM6");
    n = get(&c, "/");
    CHECK(n > 0 && !strncmp(out, "HTTP/1.1 200", 12) && strstr(out, "text/html") &&
          strstr(out, "HearBridge PS5"), "GET / serves the page");
    n = get(&c, "/api/status");
    CHECK(n > 0 && strstr(out, "application/json") && strstr(out, "\"gain_pct\":500"),
          "status JSON, default gain 500%");
    CHECK(strstr(out, "\"why\":\"\""), "status: no disconnect reason until there is one");
    CHECK(strstr(out, "WF-\\\"1000\\\"XM6") != NULL, "device name JSON-escaped");
    if (argc > 1) {
        FILE *f = fopen(argv[1], "w");
        const char *b = strstr(out, "\r\n\r\n");
        if (f && b) { fwrite(b + 4, 1, (size_t)(out + n - (b + 4)), f); fclose(f); }
    }
    post(&c, "/api/volume?pct=250");
    CHECK(c.gain_pct == 250 && c.gain_dirty, "volume 250% set + marked for saving");
    post(&c, "/api/volume?pct=999");
    CHECK(c.gain_pct == 500, "volume clamped to 500%");
    n = post(&c, "/api/volume");
    CHECK(!strncmp(out, "HTTP/1.1 400", 12), "volume without pct -> 400");
    post(&c, "/api/headset?vol=100");
    CHECK(c.req_hs_volume == 100 && c.hs_volume == 100, "headset volume request");
    post(&c, "/api/headset?pct=50");
    CHECK(c.req_hs_volume == 64, "headset volume by percent");
    post(&c, "/api/mute?on=1");
    CHECK(c.muted == 1, "mute on");
    post(&c, "/api/mute");
    CHECK(c.muted == 0, "mute toggle");
    post(&c, "/api/tone?on=1");
    CHECK(c.tone == 1, "tone on");
    post(&c, "/api/disconnect");
    CHECK(c.req_disconnect && c.paused, "disconnect");
    post(&c, "/api/connect");
    CHECK(c.req_connect && !c.paused, "connect");
    post(&c, "/api/stop");
    CHECK(c.req_stop, "stop");
    post(&c, "/api/reset");
    CHECK(c.req_reset, "reset");
    n = get(&c, "/api/status");
    CHECK(strstr(out, "\"stable\":0") && strstr(out, "\"queue_ms\":200") &&
          strstr(out, "\"latency\":{\"target_ms\":200,"), "latency: 200 ms target by default");
    c.prefs_dirty = 0;
    post(&c, "/api/latency?ms=120");
    CHECK(c.latency_ms == 120 && c.prefs_dirty, "latency: slider value set + saved per headset");
    post(&c, "/api/latency?ms=5");
    CHECK(c.latency_ms == 60, "latency: clamped to 60 ms");
    post(&c, "/api/latency?ms=99999");
    CHECK(c.latency_ms == 200, "latency: clamped to 200 ms");
    post(&c, "/api/latency?stable=0");
    CHECK(c.latency_ms == 200, "latency: old stable=0 -> 200 ms");
    post(&c, "/api/latency?stable=1");
    CHECK(c.latency_ms == 200 && strstr(out, "\"stable\":0"), "latency: old stable=1 clamps to 200 ms");
    c.lat_total = 187; c.lat_sink = 130; c.lat_sink_reported = 1;
    get(&c, "/api/status");
    CHECK(strstr(out, "\"estimate_ms\":187") && strstr(out, "\"sink_ms\":130,\"sink_reported\":1"), "latency: estimate in status");
    post(&c, "/api/latency");
    CHECK(!strncmp(out, "HTTP/1.1 400", 12), "latency without ms= -> 400");
    c.codec_avail = (1 << 1);                 /* plain SBC sink */
    c.codec_pref = 0;
    post(&c, "/api/codec?mode=3");
    CHECK(!strncmp(out, "HTTP/1.1 409", 12) && strstr(out, "not supported by this headset") && c.codec_pref == 0,
          "codec: SBC-XQ on a plain SBC headset -> refused (409), setting unchanged");
    post(&c, "/api/codec?mode=2");
    CHECK(!strncmp(out, "HTTP/1.1 409", 12) && c.codec_pref == 0, "codec: SBC HQ on a plain SBC headset -> refused");
    c.codec_avail = 0;                         /* nothing connected yet */
    post(&c, "/api/codec?mode=3");
    CHECK(!strncmp(out, "HTTP/1.1 409", 12), "codec: SBC-XQ refused while the headset's caps are unknown");
    post(&c, "/api/codec?mode=1");
    CHECK(c.codec_pref == 1, "codec: plain SBC always allowed");
    c.codec_avail = (1 << 1) | (1 << 2) | (1 << 3);   /* Xbox headset: 3f ff, bitpool 2-60 */
    post(&c, "/api/codec?mode=3");
    CHECK(c.codec_pref == 3 && c.prefs_dirty && strstr(out, "\"codec_pref\":3"), "codec: SBC-XQ picked + saved per headset");
    post(&c, "/api/codec?mode=7");
    CHECK(!strncmp(out, "HTTP/1.1 400", 12) && c.codec_pref == 3, "codec: unknown mode -> 400");
    c.prefs_dirty = 0;
    post(&c, "/api/eq?on=1&b0=6&b4=-40");
    CHECK(c.eq_on && c.eq_db[0] == 6 && c.eq_db[4] == -12 && c.prefs_dirty && c.eq_seq &&
          strstr(out, "\"eq\":{\"on\":1,\"db\":[6,0,0,0,-12]}"), "eq: bands set (clamped), saved per headset");
    post(&c, "/api/eq");
    CHECK(!strncmp(out, "HTTP/1.1 400", 12), "eq: no parameter -> 400");
    c.eq_on = 1; c.eq_db[0] = 6; c.gain_pct = 250; c.latency_ms = 1000; c.prefs_dirty = c.gain_dirty = 0;
    post(&c, "/api/clean");
    CHECK(!c.eq_on && !c.eq_db[0] && c.gain_pct == 500 && c.latency_ms == 200 && c.prefs_dirty && c.gain_dirty,
          "clean sound: EQ flat, gain 500, buffer 200, saved");
    ctl_event(&c, "switch: now SBC");
    ctl_event(&c, "stream: link dropped");
    get(&c, "/api/status");
    CHECK(strstr(out, "\"xq_low\":0") && strstr(out, "switch: now SBC") && strstr(out, "stream: link dropped"),
          "status: recent codec switch and disconnect");
    c.xq_low = 1;
    get(&c, "/api/status");
    CHECK(strstr(out, "\"xq_low\":1"), "status: low SBC-XQ note flag");
    diag_init(NULL);
    get(&c, "/api/diag");
    CHECK(!strncmp(out, "HTTP/1.1 200", 12) && strstr(out, "text/plain") &&
          strstr(out, "no diagnostics collected yet"), "diag: placeholder before collection");
    diag_set("model", "CFI-1016A");
    diag_set("tile", "RESULT ok");
    get(&c, "/api/diag");
    CHECK(strstr(out, "\r\n\r\nmodel: CFI-1016A\ntile: RESULT ok\n") != NULL, "diag: serves the report");
    get(&c, "/");
    CHECK(strstr(out, "href=\"/api/diag\"") != NULL, "page links to /api/diag");
    strcpy(c.devices_path, "/nonexistent/devices.json");
    get(&c, "/api/devices");
    CHECK(strstr(out, "\"devices\":[]") != NULL, "devices: empty list when no scan yet");
    {
        FILE *f = fopen("build/host/devices.json", "w");
        fputs("{\"version\":1,\"devices\":[{\"index\":0,\"addr\":\"AA:BB:CC:DD:EE:FF\"}]}", f);
        fclose(f);
        strcpy(c.devices_path, "build/host/devices.json");
        strcpy(c.select_path, "build/host/select.txt");
    }
    get(&c, "/api/devices");
    CHECK(strstr(out, "AA:BB:CC:DD:EE:FF") != NULL, "devices: serves devices.json");
    post(&c, "/api/select?addr=AA:BB:CC:DD:EE:FF");
    {
        char line[64] = "";
        FILE *f = fopen("build/host/select.txt", "r");
        if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
        CHECK(!strncmp(out, "HTTP/1.1 200", 12) && !strcmp(line, "AA:BB:CC:DD:EE:FF\n"),
              "select by address writes select.txt");
    }
    post(&c, "/api/select?addr=AA:BB:CC:DD:EE:F;rm");
    CHECK(!strncmp(out, "HTTP/1.1 400", 12), "select rejects a malformed address");
    post(&c, "/api/select?index=2");
    CHECK(!strncmp(out, "HTTP/1.1 200", 12), "select by index");
    {
        static const struct { const char *path, *line; } cmds[] = {
            { "/api/scan", "scan\n" },
            { "/api/reconnect", "reconnect\n" },
            { "/api/forget?addr=58:18:62:63:3B:7C", "forget 58:18:62:63:3B:7C\n" },
        };
        unsigned i;
        for (i = 0; i < sizeof cmds / sizeof *cmds; i++) {
            char line[64] = "", what[96];
            FILE *f;
            c.paused = 1;
            post(&c, cmds[i].path);
            f = fopen("build/host/select.txt", "r");
            if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
            snprintf(what, sizeof what, "%s writes \"%.*s\"", cmds[i].path,
                     (int)strlen(cmds[i].line) - 1, cmds[i].line);
            CHECK(!strncmp(out, "HTTP/1.1 200", 12) && !strcmp(line, cmds[i].line), what);
        }
        CHECK(c.paused == 1, "forget does not resume a paused session");
        post(&c, "/api/scan");
        CHECK(c.paused == 0, "scan resumes from paused");
    }
    post(&c, "/api/forget?index=1");
    CHECK(!strncmp(out, "HTTP/1.1 400", 12), "forget needs an address");
    post(&c, "/api/forget?addr=58:18:62:63:3B:7C%0Ascan");
    CHECK(!strncmp(out, "HTTP/1.1 400", 12), "forget rejects trailing junk");
    {
        FILE *f = fopen("build/host/saved.json", "w");
        fputs("{\"devices\":[{\"addr\":\"58:18:62:63:3B:7C\",\"name\":\"WF-1000XM6\",\"kind\":\"headphones\",\"current\":1}]}", f);
        fclose(f);
        strcpy(c.saved_path, "build/host/saved.json");
    }
    get(&c, "/api/saved");
    CHECK(strstr(out, "\"current\":1") && strstr(out, "application/json"), "saved: serves saved.json");
    /* POST + token for everything that changes state. */
    {
        hb_ctl d;
        char rq[320];
        int i, hex = 1;
        ctl_init(&d, "1.0.9");
        for (i = 0; i < HB_TOKEN_LEN; i++)
            if (!((c.token[i] >= '0' && c.token[i] <= '9') || (c.token[i] >= 'a' && c.token[i] <= 'f'))) hex = 0;
        CHECK(strlen(c.token) == HB_TOKEN_LEN && hex, "token: 32 hex digits");
        CHECK(strcmp(c.token, d.token) != 0, "token: a new one per start");
        get(&c, "/");
        CHECK(strstr(out, c.token) && !strstr(out, HB_TOKEN_SLOT), "page carries this run's token");
        c.muted = 0;
        get(&c, "/api/mute?on=1");
        CHECK(!strncmp(out, "HTTP/1.1 405", 12) && c.muted == 0, "GET on a state-changing endpoint -> 405, nothing changed");
        c.req_stop = 0;
        get(&c, "/api/stop");
        CHECK(!strncmp(out, "HTTP/1.1 405", 12) && !c.req_stop, "GET /api/stop -> 405");
        snprintf(rq, sizeof rq, "POST /api/mute?on=1 HTTP/1.1\r\nHost: ps5\r\n\r\n");
        http_handle(&c, rq, (int)strlen(rq), out, (int)sizeof out);
        CHECK(!strncmp(out, "HTTP/1.1 403", 12) && c.muted == 0, "POST without token -> 403");
        snprintf(rq, sizeof rq, "POST /api/stop HTTP/1.1\r\nX-HB-Token: %s\r\n\r\n", d.token);
        http_handle(&c, rq, (int)strlen(rq), out, (int)sizeof out);
        CHECK(!strncmp(out, "HTTP/1.1 403", 12) && !c.req_stop, "POST with another run's token -> 403");
        snprintf(rq, sizeof rq, "POST /api/stop HTTP/1.1\r\nX-HB-Token: %.31s\r\n\r\n", c.token);
        http_handle(&c, rq, (int)strlen(rq), out, (int)sizeof out);
        CHECK(!strncmp(out, "HTTP/1.1 403", 12) && !c.req_stop, "POST with a truncated token -> 403");
        snprintf(rq, sizeof rq, "POST /api/mute?on=1 HTTP/1.1\r\nX-Other: 1\r\nX-HB-TOKEN:   %s\r\n\r\n", c.token);
        http_handle(&c, rq, (int)strlen(rq), out, (int)sizeof out);
        CHECK(!strncmp(out, "HTTP/1.1 200", 12) && c.muted == 1, "POST with the token (any header case) -> 200");
        snprintf(rq, sizeof rq, "POST /api/mute?on=0&X-HB-Token=%s HTTP/1.1\r\n\r\n", c.token);
        http_handle(&c, rq, (int)strlen(rq), out, (int)sizeof out);
        CHECK(!strncmp(out, "HTTP/1.1 403", 12) && c.muted == 1, "token in the query string is not accepted");
        get(&c, "/api/status");
        CHECK(!strncmp(out, "HTTP/1.1 200", 12), "reads stay GET");
    }
    n = get(&c, "/nope");
    CHECK(!strncmp(out, "HTTP/1.1 404", 12), "404");
    n = http_handle(&c, "DELETE / HTTP/1.1\r\n\r\n", 21, out, sizeof out);
    CHECK(!strncmp(out, "HTTP/1.1 405", 12), "405 for other methods");
    n = http_handle(&c, "garbage", 7, out, sizeof out);
    CHECK(!strncmp(out, "HTTP/1.1 400", 12), "400 for garbage");
    (void)n;

    /* ---- gain / limiter ---- */
    CHECK(gain_parse_pct("4") == 400 && gain_parse_pct("2.5\n") == 250 &&
          gain_parse_pct("0.05") == 5 && gain_parse_pct("x") == -1 &&
          gain_parse_pct("16") == 500, "gain file parse");
    {
        char b[16];
        gain_format(250, b, sizeof b);
        CHECK(!strcmp(b, "2.50\n"), "gain file format");
    }
    {
        int16_t p[7] = { 32767, -32768, 16000, -16000, 1000, 0, 8000 };
        int i, ok = 1, peak = gain_apply_soft(p, 7, 4000);
        /* sign kept, never wraps, < full scale, monotonic */
        if (p[0] <= 0 || p[1] >= 0 || p[2] <= 0 || p[3] >= 0) ok = 0;
        for (i = 0; i < 7; i++) if (p[i] > 32766 || p[i] < -32766) ok = 0;
        if (!(p[0] >= p[2] && p[2] >= p[6] && p[6] >= p[4])) ok = 0;
        if (p[4] < 3990 || p[4] > 4010) ok = 0;     /* linear below the knee */
        CHECK(ok && peak < 1000, "soft limiter: no wrap, no hard clip, linear region");
    }
    CHECK(ctl_effective_gain_milli(400, 0, -1) == 4000 &&
          ctl_effective_gain_milli(400, 0, 127) == 4000 &&
          ctl_effective_gain_milli(400, 0, 0) == 0 &&
          ctl_effective_gain_milli(400, 1, 127) == 0 &&
          ctl_effective_gain_milli(200, 0, 64) == 1007, "headset volume maps to gain");

    /* ---- SDP ---- */
    {
        unsigned char rsp[700];
        /* ServiceSearch for AV Remote Control Target 0x110C, max 10 */
        static const unsigned char ss[] = { 0x02, 0, 1, 0, 8, 0x35, 3, 0x19, 0x11, 0x0C, 0, 10, 0 };
        /* ServiceSearchAttribute: UUID 0x110E (AVRCP), all attributes */
        static const unsigned char ssa[] = { 0x06, 0, 2, 0, 15, 0x35, 3, 0x19, 0x11, 0x0E,
            0x02, 0x00, 0x35, 5, 0x0A, 0, 0, 0xFF, 0xFF, 0 };
        static const unsigned char feat2[] = { 0x09, 0x03, 0x11, 0x09, 0x00, 0x02 };
        static const unsigned char psm17[] = { 0x19, 0x01, 0x00, 0x09, 0x00, 0x17 };
        static const unsigned char tgcl[] = { 0x35, 0x03, 0x19, 0x11, 0x0C };
        static const unsigned char v15[] = { 0x19, 0x11, 0x0E, 0x09, 0x01, 0x05 };
        n = sdp_server_handle(ss, sizeof ss, rsp, sizeof rsp);
        CHECK(n == 14 && rsp[0] == 0x03 && rsp[8] == 1 && rsp[9] == 0 && rsp[10] == 1 &&
              rsp[11] == 0 && rsp[12] == 2, "SDP: AVRCP Target record found");
        n = sdp_server_handle(ssa, sizeof ssa, rsp, sizeof rsp);
        CHECK(n > 20 && rsp[0] == 0x07 && rsp[n - 1] == 0, "SDP: search+attributes, one PDU");
        CHECK(find(rsp, n, tgcl, 5) && find(rsp, n, feat2, 6) && find(rsp, n, psm17, 6) &&
              find(rsp, n, v15, 6), "SDP: TG class, AVRCP 1.5, PSM 0x17, Category 2");
        {
            /* Same request with max 32 bytes: continuation must reassemble. */
            unsigned char req[64], all[1024];
            int tot = 0, guard = 0, full = n;
            unsigned char ref[700];
            memcpy(ref, rsp, (size_t)n);
            memcpy(req, ssa, sizeof ssa);
            req[10] = 0; req[11] = 32;
            for (;;) {
                int rl = (int)sizeof ssa - 1, cl, al;
                req[4] = (unsigned char)(rl - 5 + 1);
                n = sdp_server_handle(req, rl + 1, rsp, sizeof rsp);
                if (n < 8 || rsp[0] != 0x07) break;
                al = rsp[5] << 8 | rsp[6];
                memcpy(all + tot, rsp + 7, (size_t)al);
                tot += al;
                cl = rsp[7 + al];
                if (!cl || ++guard > 50) break;
                /* resend with continuation */
                memcpy(req, ssa, sizeof ssa - 1);
                req[10] = 0; req[11] = 32;
                req[sizeof ssa - 1] = (unsigned char)cl;
                memcpy(req + sizeof ssa, rsp + 8 + al, (size_t)cl);
                rl = (int)sizeof ssa + cl - 1;
                req[4] = (unsigned char)(rl + 1 - 5);
                n = sdp_server_handle(req, rl + 1, rsp, sizeof rsp);
                if (n < 8) break;
                al = rsp[5] << 8 | rsp[6];
                memcpy(all + tot, rsp + 7, (size_t)al);
                tot += al;
                cl = rsp[7 + al];
                if (!cl) break;
                req[sizeof ssa - 1] = (unsigned char)cl;
                memcpy(req + sizeof ssa, rsp + 8 + al, (size_t)cl);
                /* loop continues with the continuation in place */
                {
                    int k;
                    for (k = 0; k < 50 && cl; k++) {
                        rl = (int)sizeof ssa + cl - 1;
                        req[4] = (unsigned char)(rl + 1 - 5);
                        n = sdp_server_handle(req, rl + 1, rsp, sizeof rsp);
                        al = rsp[5] << 8 | rsp[6];
                        memcpy(all + tot, rsp + 7, (size_t)al);
                        tot += al;
                        cl = rsp[7 + al];
                        if (cl) memcpy(req + sizeof ssa, rsp + 8 + al, (size_t)cl);
                    }
                }
                break;
            }
            CHECK(tot == (ref[5] << 8 | ref[6]) && !memcmp(all, ref + 7, (size_t)tot) && full > 0,
                  "SDP: continuation reassembles the same attribute list");
        }
    }

    /* ---- AVRCP ---- */
    {
        avrcp_state a;
        unsigned char r[128];
        static const unsigned char caps[] = { 0x30, 0x11, 0x0E, 0x01, 0x48, 0x00,
            0x00, 0x19, 0x58, 0x10, 0x00, 0x00, 0x01, 0x03 };
        static const unsigned char reg[] = { 0x40, 0x11, 0x0E, 0x03, 0x48, 0x00,
            0x00, 0x19, 0x58, 0x31, 0x00, 0x00, 0x05, 0x0D, 0, 0, 0, 0 };
        static const unsigned char setv[] = { 0x50, 0x11, 0x0E, 0x00, 0x48, 0x00,
            0x00, 0x19, 0x58, 0x50, 0x00, 0x00, 0x01, 0x30 };
        /* headset's INTERIM then CHANGED to our registration */
        static const unsigned char interim[] = { 0x12, 0x11, 0x0E, 0x0F, 0x48, 0x00,
            0x00, 0x19, 0x58, 0x31, 0x00, 0x00, 0x02, 0x0D, 0x50 };
        static const unsigned char chg[] = { 0x12, 0x11, 0x0E, 0x0D, 0x48, 0x00,
            0x00, 0x19, 0x58, 0x31, 0x00, 0x00, 0x02, 0x0D, 0x20 };
        avrcp_init(&a, 64);
        n = avrcp_input(&a, caps, sizeof caps, r, sizeof r);
        CHECK(n == 16 && r[0] == 0x32 && r[3] == 0x0C && r[9] == 0x10 && r[13] == 0x03 &&
              r[14] == 1 && r[15] == 0x0D, "AVRCP: GET_CAPABILITIES -> VOLUME_CHANGED");
        n = avrcp_input(&a, reg, sizeof reg, r, sizeof r);
        CHECK(n == 15 && r[0] == 0x42 && r[3] == 0x0F && r[13] == 0x0D && r[14] == 64 &&
              a.notify_label == 4, "AVRCP: REGISTER_NOTIFICATION -> INTERIM 64");
        n = avrcp_input(&a, setv, sizeof setv, r, sizeof r);
        CHECK(n == 14 && r[3] == 0x09 && r[13] == 0x30 && a.volume == 0x30 && a.changed,
              "AVRCP: headset SetAbsoluteVolume accepted");
        n = avrcp_build_set_volume(&a, 100, r, sizeof r);
        CHECK(n == 14 && !(r[0] & 0x02) && r[3] == 0x00 && r[9] == 0x50 && r[13] == 100,
              "AVRCP: our SetAbsoluteVolume command");
        n = avrcp_build_volume_changed(&a, r, sizeof r);
        CHECK(n == 15 && r[0] == 0x42 && r[3] == 0x0D && r[14] == 100 && a.notify_label < 0,
              "AVRCP: CHANGED notification on the headset's label");
        n = avrcp_build_register_volume(&a, r, sizeof r);
        CHECK(n == 18 && r[3] == 0x03 && r[9] == 0x31 && r[13] == 0x0D,
              "AVRCP: our REGISTER_NOTIFICATION");
        a.changed = 0;
        avrcp_input(&a, interim, sizeof interim, r, sizeof r);
        CHECK(a.volume == 0x50 && a.remote_abs && a.ct_registered, "AVRCP: INTERIM volume read");
        avrcp_input(&a, chg, sizeof chg, r, sizeof r);
        CHECK(a.volume == 0x20 && a.changed && a.need_register, "AVRCP: CHANGED -> re-register");
        CHECK(avrcp_reported(&a), "AVRCP: a volume report counts as connected for the page");
        {
            avrcp_state quiet;
            avrcp_init(&quiet, 64);
            CHECK(!avrcp_reported(&quiet), "AVRCP: default volume alone is not a report");
        }
        CHECK(a.sink_renders, "AVRCP: headset answered our registration -> it applies the volume");
        {
            /* a headset that refuses SetAbsoluteVolume (NOT IMPLEMENTED) and never
             * took our registration: software gain */
            static const unsigned char ni[] = { 0x22, 0x11, 0x0E, 0x08, 0x48, 0x00,
                0x00, 0x19, 0x58, 0x50, 0x00, 0x00, 0x01, 0x40 };
            static const unsigned char acc[] = { 0x22, 0x11, 0x0E, 0x09, 0x48, 0x00,
                0x00, 0x19, 0x58, 0x50, 0x00, 0x00, 0x01, 0x40 };
            avrcp_init(&a, 64);
            avrcp_input(&a, acc, sizeof acc, r, sizeof r);
            CHECK(a.sink_renders && a.volume == 0x40, "AVRCP: SetAbsoluteVolume accepted -> headset applies it");
            avrcp_init(&a, 64);
            avrcp_input(&a, ni, sizeof ni, r, sizeof r);
            CHECK(!a.sink_renders, "AVRCP: SetAbsoluteVolume not implemented -> software gain");
        }
    }

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
