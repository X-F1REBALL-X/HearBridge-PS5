/* HearBridge PS5 — Bluetooth A2DP audio for the console.
 *
 * Saved device (headset.ini addr+key) → reconnect; else Inquiry → audio
 * CoD candidates → SSP pair → SDP A2DP Sink (0x110B) → AVDTP (SNK+SBC) →
 * Avcap2 capture → SBC encode → paced media packets. Streams until the
 * stop file appears; reconnects when the link drops.
 *
 * Shares the controller with the system stack (never resets it).
 * Stop: create /data/hearbridge/stop (polled every ~500 ms).
 * Headphones/speaker: pairing mode for first use; powered on afterwards.
 * Developed by X-F1REBALL-X.
 */
#include "a2dp/a2dp.h"
#include "a2dp/avdtp.h"
#include "a2dp/btlink.h"
#include "a2dp/headset_ini.h"
#include "a2dp/paired.h"
#include "a2dp/devclass.h"
#include "a2dp/rate.h"
#include "a2dp/sbc.h"
#include "a2dp/sdp_a2dp.h"
#include "avcap2.h"
#include "hci_cmd.h"
#include "hci_usb.h"
#include "acl_track.h"
#include "lock.h"
#include "log.h"
#include "notify.h"
#include "tile.h"
#include "stop.h"
#include "util.h"
#include "version.h"
#include "ctl.h"
#include "gain.h"
#include "http.h"

#include <sys/stat.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STATE_DIR "/data/hearbridge"
#define LOG_PATH  STATE_DIR "/hearbridge.log"
#define LOCK_PATH STATE_DIR "/hearbridge.lock"
#define TONE_PATH STATE_DIR "/tone"          /* exists → 1 kHz test tone */
#define GAIN_PATH STATE_DIR "/gain"          /* text: linear gain, e.g. 5 */
#define DUMP_PATH STATE_DIR "/media_dump.bin"
#define NO_TILE_PATH STATE_DIR "/no_tile"      /* exists → never add the home tile */
#define RM_TILE_PATH STATE_DIR "/remove_tile"  /* exists → remove the tile once */
#define TILE_URL_PATH STATE_DIR "/tile_url"    /* optional: deep link for the tile ("start" = fallback page) */
#define DUMP_PKTS 200

#define PCM_CAP_FRAMES  1024  /* matches Avcap2 READ_BYTES / (2*sizeof float) */

static const char g_version_tag[] __attribute__((used)) =
    "hearbridge-version " HEARBRIDGE_VERSION;
static const char g_author_tag[] __attribute__((used)) =
    "hearbridge-author X-F1REBALL-X";

static void log_choice(const char *what, const headset_ini *ini)
{
    char astr[18];
    hci_addr_str(ini->addr, astr); /* console log only — never committed */
    log_line("select: %s %s \"%s\" CoD %06x", what, astr,
             ini->name[0] ? ini->name : "-", (unsigned)ini->cod);
}

/* Encrypted ACL with the stored key, then strict SDP A2DP Sink check.
 * Returns 1 with link and psm set; 0 = connect failed; -1 = no sink. */
static int connect_abort(const unsigned char addr[6]);
/* 1 while the current attempt comes from the user (Connect / Reconnect /
 * a pick); background retries show "disconnected", not "connecting", and
 * use one short page per device. */
static int g_user_connect;
static void ctl_set_state(const char *st, const char *dev);
static int probe_link(btlink *link, headset_ini *ini, btlink **linkp, unsigned *psm);
static void idle_pump(hci_t hci, int ms);
static int g_kept_link;
static long g_av_fail_ms;   /* last AVDTP failure: the headset needs a moment */     /* the current link is the kept pairing ACL */

/* Idle: keep reading HCI events (link tracking sees a headset that
 * connects by itself) instead of sleeping; nothing is answered here. */
static void idle_pump(hci_t hci, int ms)
{
    unsigned char ev[HCI_PKT_MAX];
    if (!hci.ops || hci.ops->pump(hci.ctx, ms) < 0) { usleep((useconds_t)ms * 1000); return; }
    while (hci.ops->next_event(hci.ctx, ev, (int)sizeof ev) > 0) { }
    while (hci.ops->next_acl(hci.ctx, ev, (int)sizeof ev) > 0) { }
}

/* Accept an inbound connection from this one saved device (only while the
 * user's Connect is running). 1 = encrypted link up on `link`. */
static int accept_one(btlink *link, const headset_ini *ini, int ms)
{
    unsigned char a[1][6], k[1][16], kt[1];
    int which = -1;
    memcpy(a[0], ini->addr, 6);
    memcpy(k[0], ini->link_key, 16);
    kt[0] = ini->key_type;
    return btlink_accept(link, (const unsigned char (*)[6])a,
                         (const unsigned char (*)[16])k, kt, 1, ms, &which) && which == 0;
}

static int connect_and_probe(hci_t hci, headset_ini *ini, btlink **linkp,
                             unsigned *psm, int timeout_ms)
{
    btlink *link = NULL;

    *linkp = NULL;
    if (connect_abort(ini->addr)) {
        log_line("select: connect aborted for a page command");
        return 0;
    }
    if (g_av_fail_ms && now_ms() - g_av_fail_ms < 2500) {
        long w = g_av_fail_ms + 2500;
        log_line("select: waiting %ld ms after the last AVDTP failure", w - now_ms());
        while (now_ms() < w) idle_pump(hci, 50);
    }
    link = btlink_create(hci, 1021, 7);
    if (!link) return 0;
    (void)btlink_drop_stale(link, ini->addr);
    (void)btlink_pump(link, 50);              /* take in queued events (tracking) */
    {
        /* The headset may have connected to the controller by itself (it
         * pages its last host when it leaves the case). A link we did not
         * open: close it, then page. A request still waiting: accept it. */
        unsigned h = acl_track_handle(ini->addr);
        long age = acl_track_request_age(ini->addr, now_ms());
        if (h) {
            log_line("select: an ACL to this device already exists (handle %#05x) — closing it", h);
            (void)btlink_drop_handle(link, h, 2500);
        } else if (age >= 0 && age < 8000) {
            if (accept_one(link, ini, 6000)) return probe_link(link, ini, linkp, psm);
        }
    }
    if (!btlink_connect(link, ini->addr, 0x01, 0, ini->link_key, ini->key_type,
                        ini->name, (int)sizeof ini->name, timeout_ms)) {
        int ok = 0;
        if (btlink_last_connect_fail() == 0x04 && !connect_abort(ini->addr)) {
            /* Page timeout: the device may connect in on power-up. Listen
             * for its own request during this Connect only. */
            log_line("select: page timeout — waiting 8 s for the device to connect in");
            ok = accept_one(link, ini, 8000);
        } else if (btlink_last_connect_fail() == 0x0B || btlink_last_connect_fail() == 0) {
            unsigned h = acl_track_handle(ini->addr);
            if (h) {
                /* 0x0b: a link exists after all — close that handle, page once more. */
                log_line("select: 0x0b — closing the existing link %#05x and paging again", h);
                (void)btlink_drop_handle(link, h, 2500);
                ok = btlink_connect(link, ini->addr, 0x01, 0, ini->link_key, ini->key_type,
                                    ini->name, (int)sizeof ini->name, timeout_ms);
            } else {
                /* Handle unknown: the headset is calling us. Accept it. */
                log_line("select: 0x0b with no known handle — the link was opened outside this app "
                         "(no Connection Complete reached us; HCI has no safe handle->address lookup, "
                         "so no other handle is touched). Waiting for the headset to connect in");
                ok = accept_one(link, ini, 8000);
            }
        }
        if (!ok) {
            btlink_destroy(link);
            return 0;
        }
    }
    if (!link) return 0;
    return probe_link(link, ini, linkp, psm);
}

/* SDP check on an encrypted link; on success the link is handed out. */
static int probe_link(btlink *link, headset_ini *ini, btlink **linkp, unsigned *psm)
{
    int sdp;
    {
        long w = now_ms() + 500;
        while (now_ms() < w)
            if (btlink_pump(link, 40) < 0) break;
    }
    if (ini->ok && (btlink_chan_find_inbound(link, BTLINK_PSM_AVDTP) ||
                    btlink_chan_find_inbound(link, BTLINK_PSM_SDP))) {
        /* A saved audio device that opened SDP/AVDTP to us itself: no need
         * for our own SDP on top of its channels (it can stall them). */
        log_line("select: headset opened its own channels — skipping our SDP");
        *psm = BTLINK_PSM_AVDTP;
        *linkp = link;
        return 1;
    }
    sdp = sdp_probe_a2dp_sink(link, 12000, psm);
    if (sdp == 0) {
        log_choice("no A2DP Sink in SDP — rejecting", ini);
        btlink_disconnect(link);
        btlink_destroy(link);
        return -1;
    }
    if (sdp < 0)
        log_line("select: SDP inconclusive — trying AVDTP on PSM %#x "
                 "(Discover must show an Audio SNK)", *psm);
    *linkp = link;
    return 1;
}

#define DEVICES_JSON  STATE_DIR "/devices.json"
#define SELECT_TXT    STATE_DIR "/select.txt"
#define STATUS_TXT    STATE_DIR "/status.txt"
#define PAIRED_INI    STATE_DIR "/paired.ini"
#define SAVED_JSON    STATE_DIR "/saved.json"
#define RETRY_SAVED_S 10    /* (unused: no background paging) */
#define SELECT_WAIT_S 86400 /* manual mode: wait for a choice indefinitely */
/* Scans run only when the user presses Scan: inquiries back to back for
 * SCAN_WINDOW_S, then the list stays until the next press. */
#define SCAN_WINDOW_S 20

static void write_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void write_status(const char *fmt, ...)
{
    char line[128];
    FILE *f;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    /* Mirror to the web page: state = first word, detail = whole line. */
    CTL_LOCK(&g_ctl);
    snprintf(g_ctl.detail, sizeof g_ctl.detail, "%s", line);
    {
        size_t i = 0;
        while (line[i] && line[i] != ' ' && i + 1 < sizeof g_ctl.state) {
            g_ctl.state[i] = line[i];
            i++;
        }
        g_ctl.state[i] = 0;
    }
    CTL_UNLOCK(&g_ctl);
    f = fopen(STATUS_TXT ".tmp", "w");
    if (!f) return;
    fputs(line, f);
    fputc('\n', f);
    fclose(f);
    rename(STATUS_TXT ".tmp", STATUS_TXT);
}

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') fprintf(f, "\\%c", ch);
        else if (ch < 0x20) fprintf(f, "\\u%04x", ch);
        else fputc(ch, f);
    }
    fputc('"', f);
}

/* Runtime-only list of audio candidates for the chooser page. Written
 * atomically (tmp + rename). Addresses live only on the console. */
static int g_dev_quiet;               /* live updates during a scan: no log line */

static void write_devices(const a2dp_inq_dev *found, const int *order, int n)
{
    FILE *f = fopen(DEVICES_JSON ".tmp", "w");
    int i, w = 0;
    if (!f) {
        log_line("select: cannot write %s", DEVICES_JSON);
        return;
    }
    fprintf(f, "{\n  \"version\": 1,\n  \"scanned_ms\": %ld,\n  \"devices\": [", now_ms());
    for (i = 0; i < n; i++) {
        const a2dp_inq_dev *d = &found[order[i]];
        char astr[18];
        if (hb_dev_rank(d->cod, d->name) < 0) continue;   /* never list TVs, phones, car kits */
        hci_addr_str(d->addr, astr);
        fprintf(f, "%s\n    {\"index\": %d, \"addr\": \"%s\", \"name\": ",
                w++ ? "," : "", i, astr);
        json_str(f, d->name);
        {
            const char *k = hb_dev_kind(d->cod, d->name);
            fprintf(f, ", \"cod\": \"%06x\", \"kind\": \"%s\", \"rssi\": ", (unsigned)d->cod,
                    k ? k : "audio");
        }
        if (d->have_rssi) fprintf(f, "%d}", d->rssi);
        else fprintf(f, "null}");
    }
    fprintf(f, "\n  ]\n}\n");
    fclose(f);
    rename(DEVICES_JSON ".tmp", DEVICES_JSON);
    if (!g_dev_quiet) log_line("select: wrote %d device(s) to %s", w, DEVICES_JSON);
}


/* ---- last-seen devices: merged within a scan, kept until the next one -- */
static a2dp_inq_dev g_seen[A2DP_INQ_MAX];
static long g_seen_ms[A2DP_INQ_MAX];
static int g_nseen;

static void seen_clear(void)
{
    g_nseen = 0;
}

static void seen_merge(const a2dp_inq_dev *devs, int n)
{
    long now = now_ms();
    int i, j;
    /* No time expiry: what the last scan found stays listed until the
     * next Scan press (seen_clear). */
    for (i = 0; i < n; i++) {
        for (j = 0; j < g_nseen; j++)
            if (!memcmp(g_seen[j].addr, devs[i].addr, 6)) break;
        if (j == g_nseen) {
            if (g_nseen >= A2DP_INQ_MAX) continue;
            g_nseen++;
            g_seen[j] = devs[i];
        } else {
            char keep[A2DP_NAME_MAX];
            memcpy(keep, g_seen[j].name, sizeof keep);
            g_seen[j] = devs[i];
            if (!g_seen[j].name[0]) memcpy(g_seen[j].name, keep, sizeof keep);
        }
        g_seen_ms[j] = now;
    }
}

/* Merge results and rewrite devices.json from the last-seen list. */
static int seen_publish(const a2dp_inq_dev *devs, int n, a2dp_inq_dev *found,
                        int *order, int *nfound)
{
    int ncand;
    seen_merge(devs, n);
    memcpy(found, g_seen, sizeof g_seen);
    *nfound = g_nseen;
    ncand = a2dp_rank_sinks(found, *nfound, NULL, order, A2DP_INQ_MAX);
    write_devices(found, order, ncand);
    return ncand;
}

static void inquiry_progress(const a2dp_inq_dev *devs, int n)
{
    a2dp_inq_dev f[A2DP_INQ_MAX];
    int o[A2DP_INQ_MAX], nf;
    g_dev_quiet = 1;
    (void)seen_publish(devs, n, f, o, &nf);
    g_dev_quiet = 0;
}

/* ---- web commands (select.txt) and the saved-device list ------------- */
enum { CMD_NONE, CMD_ADDR, CMD_INDEX, CMD_SCAN, CMD_RECONNECT };
typedef struct { int kind, index; unsigned char addr[6]; } hb_cmd;

static headset_ini g_paired[PAIRED_MAX];
static int g_npaired;
static hb_cmd g_pending;              /* command that ended the last session */

static void publish_saved(const headset_ini *cur)
{
    char js[4096];
    int n = paired_json(g_paired, g_npaired, cur && cur->ok ? cur->addr : NULL, js, sizeof js);
    FILE *f;
    if (n < 0 || !(f = fopen(SAVED_JSON ".tmp", "w"))) return;
    fwrite(js, 1, (size_t)n, f);
    fclose(f);
    rename(SAVED_JSON ".tmp", SAVED_JSON);
}

/* The device the user picked last wins: for WANT_HOLD_S after a pick,
 * auto-reconnect only tries that one, so it cannot steal the link back
 * to the previous headset. */
#define WANT_HOLD_S 120
static unsigned char g_want[6];
static long g_want_until;

static void want_device(const unsigned char addr[6])
{
    memcpy(g_want, addr, 6);
    g_want_until = now_ms() + WANT_HOLD_S * 1000L;
}

static int want_blocks(const unsigned char addr[6])
{
    return g_want_until && now_ms() < g_want_until && memcmp(g_want, addr, 6) != 0;
}

/* Saved devices the user disconnected by hand: auto-reconnect leaves them
 * alone until Connect is pressed for them (or HearBridge restarts). */
static unsigned char g_hold[PAIRED_MAX][6];
static int g_nhold;

static int held(const unsigned char a[6])
{
    int i;
    for (i = 0; i < g_nhold; i++) if (!memcmp(g_hold[i], a, 6)) return 1;
    return 0;
}

static void hold_add(const unsigned char a[6])
{
    if (held(a) || g_nhold >= PAIRED_MAX) return;
    memcpy(g_hold[g_nhold++], a, 6);
    log_line("saved: auto-reconnect paused for the disconnected device");
}

static void hold_clear(const unsigned char a[6])
{
    int i;
    for (i = 0; i < g_nhold; i++)
        if (!memcmp(g_hold[i], a, 6)) { memmove(g_hold[i], g_hold[i + 1], (size_t)(g_nhold - i - 1) * 6); g_nhold--; return; }
}


/* Never lose a good name/CoD: fill blanks from the saved entry or the scan. */
static void keep_identity(headset_ini *d)
{
    int i, s = paired_find(g_paired, g_npaired, d->addr);
    int blank = !d->name[0] || !strcmp(d->name, "-");
    if (s >= 0) {
        if (blank && g_paired[s].name[0] && strcmp(g_paired[s].name, "-")) {
            snprintf(d->name, sizeof d->name, "%s", g_paired[s].name); blank = 0;
        }
        if (!d->cod) d->cod = g_paired[s].cod;
    }
    for (i = 0; i < g_nseen; i++)
        if (!memcmp(g_seen[i].addr, d->addr, 6)) {
            if (blank && g_seen[i].name[0]) { snprintf(d->name, sizeof d->name, "%s", g_seen[i].name); blank = 0; }
            if (!d->cod) d->cod = g_seen[i].cod;
        }
}

static void remember_device(const headset_ini *din)
{
    headset_ini dd = *din, *d = &dd;
    if (!d->ok) return;
    keep_identity(d);
    paired_put(g_paired, &g_npaired, PAIRED_MAX, d);
    if (!paired_save(PAIRED_INI, g_paired, g_npaired))
        log_line("saved: cannot write %s", PAIRED_INI);
    publish_saved(d);
}

/* Makes saved entry i the current headset (headset.ini). */
static int use_saved(int i, headset_ini *ini)
{
    if (i < 0 || i >= g_npaired) return 0;
    *ini = g_paired[i];
    ini->ok = ini->have_addr = 1;
    keep_identity(ini);
            if (!headset_ini_save(ini)) log_line("saved: cannot write headset.ini");
    publish_saved(ini);
    return 1;
}

/* A page command is waiting: long scans stop early for it. */
static unsigned g_cmd_seen;           /* last g_ctl.cmd_seq taken by poll_cmd */

static int cmd_waiting(void)
{
    unsigned seq;
    CTL_LOCK(&g_ctl);
    seq = g_ctl.cmd_seq;
    CTL_UNLOCK(&g_ctl);
    return seq != g_cmd_seen;
}

/* Stop the page / listen in progress for addr: a background attempt yields
 * to any page command; a user attempt yields to a Forget of it, a pick of
 * another device, Scan or Reconnect. */
static int connect_abort(const unsigned char addr[6])
{
    FILE *f;
    char line[64];
    unsigned char a[6];
    int stop = 1;
    if (!cmd_waiting()) return 0;
    if (!g_user_connect) return 1;
    f = fopen(SELECT_TXT, "r");
    if (!f) return 0;
    if (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "forget ", 7))
            stop = headset_parse_addr(line + 7, a) && !memcmp(a, addr, 6);
        else if (headset_parse_addr(line, a))
            stop = memcmp(a, addr, 6) != 0;      /* same device again: keep going */
    }
    fclose(f);
    return stop;
}

static int btlink_forget_abort(const unsigned char addr[6])
{
    return connect_abort(addr);
}

/* A saved device answered a page or connected in: show "connecting" now. */
static void on_acl_up(const unsigned char addr[6])
{
    int i = paired_find(g_paired, g_npaired, addr);
    const char *nm = i >= 0 && g_paired[i].name[0] ? g_paired[i].name : "-";
    write_status("connecting %s", nm);
    ctl_set_state("connecting", i >= 0 ? g_paired[i].name : NULL);
}

/* Reads and deletes select.txt. "forget" is handled here; forgetting the
 * current headset turns into a scan. */
static int poll_cmd(hb_cmd *c, headset_ini *ini)
{
    FILE *f;
    char line[64];
    CTL_LOCK(&g_ctl);
    g_cmd_seen = g_ctl.cmd_seq;           /* consumed, whether or not the file reads */
    CTL_UNLOCK(&g_ctl);
    f = fopen(SELECT_TXT, "r");
    memset(c, 0, sizeof *c);
    if (!f) return 0;
    if (!fgets(line, sizeof line, f)) line[0] = 0;
    fclose(f);
    unlink(SELECT_TXT);
    line[strcspn(line, "\r\n")] = 0;
    if (!strcmp(line, "scan")) c->kind = CMD_SCAN;
    else if (!strcmp(line, "reconnect")) c->kind = CMD_RECONNECT;
    else if (!strncmp(line, "forget ", 7) && headset_parse_addr(line + 7, c->addr)) {
        int cur = ini->ok && !memcmp(ini->addr, c->addr, 6);
        int fi = paired_find(g_paired, g_npaired, c->addr), shown = 0;
        if (fi >= 0 && g_paired[fi].name[0]) {
            CTL_LOCK(&g_ctl);
            shown = !strcmp(g_ctl.device, g_paired[fi].name);
            CTL_UNLOCK(&g_ctl);
        }
        /* shown: it is the one being connected right now (maybe not headset.ini) */
        if (paired_drop(g_paired, &g_npaired, c->addr)) paired_save(PAIRED_INI, g_paired, g_npaired);
        log_line("saved: forgot a device%s", cur || shown ? " (the current one)" : "");
        hold_clear(c->addr);
        if (!memcmp(g_want, c->addr, 6)) g_want_until = 0;
        if (cur || shown) {
            if (cur) {
                unlink(HEADSET_INI_PATH);
                memset(ini, 0, sizeof *ini);
            }
            c->kind = CMD_SCAN;            /* disconnect it and show the chooser */
            ctl_set_state("scanning", NULL);
            CTL_LOCK(&g_ctl);
            g_ctl.device[0] = 0;
            CTL_UNLOCK(&g_ctl);
            write_status("scanning");
        }
        publish_saved(ini);
    } else if (headset_parse_addr(line, c->addr)) c->kind = CMD_ADDR;
    else if (line[0] >= '0' && line[0] <= '9') { c->kind = CMD_INDEX; c->index = atoi(line); }
    else if (line[0]) log_line("select: ignoring \"%s\"", line);
    if (c->kind != CMD_NONE) log_line("select: page command received: \"%s\"", line);
    return c->kind != CMD_NONE;
}

static int connect_and_probe(hci_t hci, headset_ini *ini, btlink **linkp,
                             unsigned *psm, int timeout_ms);

/* Status label for a connect_and_probe failure. */
static const char *conn_fail_label(int r)
{
    if (r < 0) return "not-a2dp-sink";
    if (btlink_last_connect_fail() == 0x04) return "page-timeout";
    if (btlink_last_connect_fail() == 0x0B) return "link-held-elsewhere";
    return "connect-failed";
}
static void ctl_set_state(const char *st, const char *dev);

/* Pair the chosen device (saves headset.ini), reconnect, SDP-check. */
static int try_device(a2dp_session *asess, hci_t hci, const a2dp_inq_dev *d,
                      headset_ini *ini, btlink **linkp, unsigned *psm)
{
    a2dp_pair_result pr;

    log_line("select: trying CoD %06x \"%s\"", (unsigned)d->cod,
             d->name[0] ? d->name : "-");
    write_status("pairing %s", d->name[0] ? d->name : "-");
    notify("HearBridge: pairing\n%s", d->name[0] ? d->name : "audio device");
    memset(&pr, 0, sizeof pr);
    *linkp = NULL;
    a2dp_pair_keep_acl = 1;
    {
        int okp = a2dp_pair(asess, d, &pr);
        a2dp_pair_keep_acl = 0;
        if (!okp || !headset_ini_load(ini)) {
            if (okp && pr.handle >= 0) {          /* no ini: release the kept link */
                btlink *t = btlink_create(hci, 1021, 7);
                if (t) { (void)btlink_drop_handle(t, (unsigned)pr.handle, 1500); btlink_destroy(t); }
            }
            write_status("error pair-failed");
            return 0;
        }
    }
    if (d->name[0] && (!ini->name[0] || !strcmp(ini->name, "-")))
        snprintf(ini->name, sizeof ini->name, "%s", d->name);
    if (!ini->cod) ini->cod = d->cod;
    keep_identity(ini);
    if (!headset_ini_save(ini)) log_line("saved: cannot write headset.ini");
    remember_device(ini);              /* paired: keep the key even if the next step fails */
    if (pr.handle >= 0) {
        /* Stay on the pairing link (some headsets stop answering pages once
         * it drops). Fresh L2CAP state, ~300 ms settle, then SDP. */
        btlink *k = btlink_create(hci, 1021, 7);
        int sdp = 0;
        if (k && btlink_adopt(k, (unsigned)pr.handle, ini->addr, ini->link_key,
                              ini->key_type, ini->name)) {
            long w = now_ms() + 300;
            while (now_ms() < w) if (btlink_pump(k, 30) < 0) break;
            if (btlink_chan_find_inbound(k, BTLINK_PSM_AVDTP)) {
                log_line("select: headset opened AVDTP on the pairing link — skipping SDP");
                *psm = BTLINK_PSM_AVDTP;
                sdp = 1;
            } else if (btlink_is_up(k)) {
                /* One quick SDP try; a stuck channel means re-page instead. */
                btlink_set_cfg_timeout(k, 3500);
                sdp_one_round = 1;
                sdp = sdp_probe_a2dp_sink(k, 4000, psm);
                sdp_one_round = 0;
                btlink_set_cfg_timeout(k, 0);
            } else {
                sdp = -2;
            }
            if (sdp == 0) {
                log_choice("no A2DP Sink in SDP — rejecting", ini);
                btlink_disconnect(k);
                btlink_destroy(k);
                write_status("error not-a2dp-sink");
                return 0;
            }
            if (sdp > 0) {
                *linkp = k;
                g_kept_link = 1;
                log_choice("chosen", ini);
                write_status("connected %s", ini->name[0] ? ini->name : "-");
                return 1;
            }
        }
        if (k && !btlink_is_up(k) && !connect_abort(ini->addr)) {
            /* The headset itself dropped the new link: it usually comes
             * back on its own. Listen for it (stored key) before paging. */
            log_line("select: headset closed the pairing link — waiting 12 s for it to connect in");
            btlink_destroy(k);
            k = btlink_create(hci, 1021, 7);          /* fresh state for the new link */
            if (k && accept_one(k, ini, 12000)) {
                int pr2 = probe_link(k, ini, linkp, psm);
                if (pr2 == 1) {
                    log_choice("chosen", ini);
                    write_status("connected %s", ini->name[0] ? ini->name : "-");
                    return 1;
                }
                if (pr2 < 0) { write_status("error not-a2dp-sink"); return 0; }  /* k freed */
            }
        }
        /* SDP did not work on the kept link: close it, page as before. */
        log_line("select: SDP on the pairing link failed (%d) — closing it and paging", sdp);
        if (k) {
            if (btlink_is_up(k)) btlink_disconnect(k);
            /* down already (headset closed it): nothing to close — handles
             * are reused controller-wide, never close one blindly */
            btlink_destroy(k);
        }
        {
            long w = now_ms() + 1000;
            while (now_ms() < w) idle_pump(hci, 50);
        }
    }
    {
        int r = connect_and_probe(hci, ini, linkp, psm, 45000);
        if (r != 1) {
            write_status("error %s", conn_fail_label(r));
            return 0;
        }
    }
    log_choice("chosen", ini);
    remember_device(ini);
    write_status("connected %s", ini->name[0] ? ini->name : "-");
    return 1;
}


/* Reconnect with a saved key (no pairing). 1 = link ready. */
static int try_saved(hci_t hci, headset_ini *ini, btlink **linkp, unsigned *psm, int timeout_ms)
{
    int r;
    if (!ini->ok) return 0;
    log_choice("reconnecting saved", ini);
    if (g_user_connect) {
        write_status("connecting %s", ini->name[0] ? ini->name : "-");
        ctl_set_state("connecting", ini->name);
    } else {
        write_status("disconnected waiting for %s", ini->name[0] ? ini->name : "-");
        ctl_set_state("disconnected", ini->name);
    }
    r = connect_and_probe(hci, ini, linkp, psm, timeout_ms);
    if (r == 1) {
        remember_device(ini);
        return 1;
    }
    log_line("select: saved device did not answer (%s)", conn_fail_label(r));
    return 0;
}

/* Tries every saved headset, most recently used first (the list order).
 * Stops early when the page sends a command (left in g_pending). */
static int try_all_saved(hci_t hci, headset_ini *ini, btlink **linkp, unsigned *psm,
                         int first_ms, int rest_ms)
{
    int i, n = g_npaired;
    headset_ini order[PAIRED_MAX];
    memcpy(order, g_paired, sizeof order);
    for (i = 0; i < n && !hb_stop_requested(); i++) {
        headset_ini cand = order[i];
        hb_cmd c;
        cand.ok = cand.have_addr = 1;
        if (held(cand.addr)) {
            log_line("rotation: skip \"%s\" (disconnected by hand)", cand.name);
            continue;
        }
        if (want_blocks(cand.addr)) {
            log_line("rotation: skip \"%s\" (another device was just picked)", cand.name);
            continue;
        }
        if (paired_find(g_paired, g_npaired, cand.addr) < 0) continue;   /* forgotten meanwhile */
        log_line("rotation: %d/%d \"%s\"", i + 1, n, cand.name);
        if (try_saved(hci, &cand, linkp, psm, i ? rest_ms : first_ms)) {
            *ini = cand;
            keep_identity(ini);
            if (!headset_ini_save(ini)) log_line("saved: cannot write headset.ini");
            publish_saved(ini);
            return 1;
        }
        if (poll_cmd(&c, ini)) { g_pending = c; return 0; }
    }
    return 0;
}

/* Acts on a pick (address, list index) from the page. */
static int try_pick(a2dp_session *asess, hci_t hci, const hb_cmd *c,
                    const a2dp_inq_dev *found, const int *order, int n,
                    headset_ini *ini, btlink **linkp, unsigned *psm)
{
    a2dp_inq_dev pick;
    int i;
    g_user_connect = 1;
    memset(&pick, 0, sizeof pick);
    if (c->kind == CMD_INDEX) {
        if (c->index < 0 || c->index >= n) return 0;
        pick = found[order[c->index]];
        want_device(pick.addr);
        hold_clear(pick.addr);
    } else {
        want_device(c->addr);
        hold_clear(c->addr);
        int s = paired_find(g_paired, g_npaired, c->addr);
        if (s >= 0) {                              /* already paired: use the key */
            use_saved(s, ini);
            if (try_saved(hci, ini, linkp, psm, 30000)) return 1;   /* one attempt */
            write_status("error %s %s", conn_fail_label(0), ini->name[0] ? ini->name : "-");
            log_line("select: saved device did not connect — is it on?");
            return 0;
        }
        for (i = 0; i < n; i++)
            if (!memcmp(found[order[i]].addr, c->addr, 6)) pick = found[order[i]];
        if (memcmp(pick.addr, c->addr, 6)) {       /* not in the last scan */
            memcpy(pick.addr, c->addr, 6);
            pick.psrm = 0x01;
        }
    }
    publish_saved(NULL);                           /* the old one is no longer current */
    ctl_set_state("connecting", pick.name[0] ? pick.name : NULL);
    return try_device(asess, hci, &pick, ini, linkp, psm);
}

/* Chooser: scan, publish devices.json, act on the page's commands. The
 * saved headset (if any) is retried every RETRY_SAVED_S and on Reconnect. */
static int discover_and_select(a2dp_session *asess, hci_t hci, headset_ini *ini,
                               btlink **linkp, unsigned *psm, int scan_now)
{
    a2dp_inq_dev found[A2DP_INQ_MAX];
    int order[A2DP_INQ_MAX];
    int nfound = 0, ncand = 0;
    long t_end = now_ms() + SELECT_WAIT_S * 1000L, t_keep_err = -100000;
    long scan_until = 0;                   /* a Scan press searches for SCAN_WINDOW_S */
    hb_cmd c;

    if (scan_now) {
        seen_clear();
        scan_until = now_ms() + SCAN_WINDOW_S * 1000L;
        a2dp_scan_deadline_ms = scan_until;
    } else {
        if (now_ms() - t_keep_err > 10000 && strncmp(g_ctl.detail, "error", 5)) {
            write_status("disconnected");
            ctl_set_state("disconnected", NULL);
        }
        if (g_nseen) {                     /* keep the last list on the page */
            nfound = g_nseen;
            memcpy(found, g_seen, sizeof g_seen);
            ncand = a2dp_rank_sinks(found, nfound, NULL, order, A2DP_INQ_MAX);
        }
    }
    if (!ini->ok) notify("HearBridge: choose a device\nPut it in pairing mode");
    while (now_ms() < t_end) {
        if (hb_stop_requested()) return 0;
        if (now_ms() < scan_until && !cmd_waiting()) {
            a2dp_inq_dev got[A2DP_INQ_MAX];
            int ngot = 0;
            if (now_ms() - t_keep_err > 10000) write_status("scanning");
            memset(got, 0, sizeof got);
            if (!a2dp_inquiry(asess, got, A2DP_INQ_MAX, &ngot)) {
                if (hb_stop_requested()) return 0;
                write_status("error inquiry-failed");
                return 0;
            }
            /* Full or cut short: everything seen goes into devices.json. */
            ncand = seen_publish(got, ngot, found, order, &nfound);
            /* Inquiries are chained for the whole window ("scanning"),
             * then the list stays and the page shows Not connected. */
            if (now_ms() >= scan_until - 600) {     /* deadline: done */
                scan_until = 0;
                a2dp_scan_deadline_ms = 0;
                log_line("scan: finished (%d device(s) listed)", ncand);
                if (now_ms() - t_keep_err > 10000) write_status("waiting-selection %d", ncand);
            }
        }
        {
            int go;
            CTL_LOCK(&g_ctl);
            go = g_ctl.req_connect;
            g_ctl.req_connect = 0;
            if (go) g_ctl.paused = 0;
            CTL_UNLOCK(&g_ctl);
            if (go && !cmd_waiting()) {
                /* plain Connect: the current (or most recent) saved device, once */
                headset_ini cand = *ini;
                if (!cand.ok && g_npaired) { cand = g_paired[0]; cand.ok = cand.have_addr = 1; }
                if (cand.ok) {
                    g_user_connect = 1;
                    hold_clear(cand.addr);
                    if (try_saved(hci, &cand, linkp, psm, 30000)) {
                        *ini = cand;
                        keep_identity(ini);
            if (!headset_ini_save(ini)) log_line("saved: cannot write headset.ini");
                        publish_saved(ini);
                        g_user_connect = 0;
                        return 1;
                    }
                    g_user_connect = 0;
                    write_status("error %s %s", conn_fail_label(0), cand.name[0] ? cand.name : "-");
                    t_keep_err = now_ms();
                }
            }
        }
        if (poll_cmd(&c, ini)) {
            int ok = 0;
            if (c.kind != CMD_SCAN) { scan_until = 0; a2dp_scan_deadline_ms = 0; }
            t_end = now_ms() + SELECT_WAIT_S * 1000L;
            if (c.kind == CMD_SCAN) {
                seen_clear();
                scan_until = now_ms() + SCAN_WINDOW_S * 1000L;
        a2dp_scan_deadline_ms = scan_until;
                continue;
            }
            g_user_connect = 1;
            if (c.kind == CMD_RECONNECT) ok = try_all_saved(hci, ini, linkp, psm, 20000, 12000);
            else ok = try_pick(asess, hci, &c, found, order, ncand, ini, linkp, psm);
            if (ok) return 1;
            if (c.kind != CMD_ADDR) write_status("waiting-selection %d", ncand);
            else t_keep_err = now_ms();
        }
        if (!cmd_waiting()) idle_pump(hci, 100);
    }
    log_line("select: no choice within %d s", SELECT_WAIT_S);
    write_status("error selection-timeout");
    return 0;
}

/* Soft ~440 Hz triangle (no libm) into interleaved stereo s16; amp=0 → silence. */
static void fill_tone_s16(int16_t *out, int frames, int sample_rate,
                          double *phase, float amp)
{
    int i;
    /* phase in [0,1); step ≈ 440/sample_rate */
    double step = 440.0 / (double)(sample_rate > 0 ? sample_rate : 48000);
    int16_t s;

    if (amp < 0.f) amp = 0.f;
    if (amp > 0.4f) amp = 0.4f;
    for (i = 0; i < frames; i++) {
        float v = 0.f;
        if (amp > 0.f) {
            /* triangle: 0..0.5 → -1..+1, 0.5..1 → +1..-1 */
            double ph = *phase;
            float tri = ph < 0.5 ? (float)(ph * 4.0 - 1.0)
                                 : (float)(3.0 - ph * 4.0);
            v = tri * amp;
        }
        {
            int sample = (int)(v * 32767.f);
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            s = (int16_t)sample;
        }
        out[i * 2] = s;
        out[i * 2 + 1] = s;
        *phase += step;
        if (*phase >= 1.0) *phase -= 1.0;
    }
}

/* sin(2*pi*x) for x in [0,1) without libm (odd polynomial, |err| < 1e-6). */
static double sin_turns(double x)
{
    double t, t2;
    x -= (double)(long)x;
    if (x < 0) x += 1.0;
    /* fold to [-0.25, 0.25] turns */
    if (x > 0.75) x -= 1.0;
    else if (x > 0.25) x = 0.5 - x;
    t = x * 6.283185307179586;
    t2 = t * t;
    return t * (1 - t2 / 6 * (1 - t2 / 20 * (1 - t2 / 42 * (1 - t2 / 72 *
           (1 - t2 / 110)))));
}

/* 1 kHz sine at -6 dBFS (0.5 full scale), both channels. */
static void fill_sine_1k(int16_t *out, int frames, int sample_rate, double *phase)
{
    double step = 1000.0 / (double)(sample_rate > 0 ? sample_rate : 48000);
    int i;
    for (i = 0; i < frames; i++) {
        int16_t v = (int16_t)(sin_turns(*phase) * 16383.0);
        out[i * 2] = out[i * 2 + 1] = v;
        *phase += step;
        if (*phase >= 1.0) *phase -= 1.0;
    }
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void ctl_set_state(const char *st, const char *dev)
{
    CTL_LOCK(&g_ctl);
    snprintf(g_ctl.state, sizeof g_ctl.state, "%s", st);
    if (dev) snprintf(g_ctl.device, sizeof g_ctl.device, "%s", dev);
    if (strcmp(st, "streaming")) ctl_clear_link(&g_ctl, !strcmp(st, "paused"));
    CTL_UNLOCK(&g_ctl);
}

/* Base gain percent from GAIN_PATH; default 500 (x5) when absent. */
static int read_gain_pct(void)
{
    FILE *f = fopen(GAIN_PATH, "r");
    char buf[32];
    int pct = -1;
    if (f) {
        if (fgets(buf, sizeof buf, f)) pct = gain_parse_pct(buf);
        fclose(f);
    }
    return pct < 0 ? HB_GAIN_DEFAULT_PCT : pct;
}

static void write_gain_pct(int pct)
{
    char buf[16];
    FILE *f = fopen(GAIN_PATH ".tmp", "w");
    if (!f) return;
    gain_format(pct, buf, sizeof buf);
    fputs(buf, f);
    fclose(f);
    rename(GAIN_PATH ".tmp", GAIN_PATH);
}

static void persist_gain_if_dirty(void)
{
    int pct = -1;
    CTL_LOCK(&g_ctl);
    if (g_ctl.gain_dirty) { pct = g_ctl.gain_pct; g_ctl.gain_dirty = 0; }
    CTL_UNLOCK(&g_ctl);
    if (pct >= 0) {
        write_gain_pct(pct);
        log_line("volume: base gain %d%% saved", pct);
    }
}

/* Encode + send up to max_sbc frames from pcm[frames]. Returns packets sent. */
/* Packs SBC frames into media packets of up to per_pkt frames. */
typedef struct {
    avdtp_session *av;
    sbc_encoder *enc;
    btlink *link;
    hb_rate rate;
    int fsz, samples_per, per_pkt, mtu;
    int max_pp;          /* tuned frames/packet ceiling (0 = MTU fit) */
    int rate_hz;
    unsigned char buf[HCI_PKT_MAX];
    int nbytes, nframes;
    long pkts, frames;
} packer;

/* As many whole frames as the media MTU holds (RTP 12 + SBC header 1;
 * the NUM field is 4 bits, so at most 15), from the frame length at the
 * bitpool in use. */
static void packer_size(packer *p)
{
    p->fsz = (int)sbc_encoder_frame_bytes(p->enc);
    if (p->fsz <= 0) p->fsz = 29;
    p->per_pkt = hb_frames_per_packet(p->mtu, p->fsz);   /* always MTU-capped */
    if (p->per_pkt * p->fsz > (int)sizeof p->buf) p->per_pkt = (int)sizeof p->buf / p->fsz;
    if (p->per_pkt < 1) p->per_pkt = 1;
    if (p->max_pp > 0 && p->per_pkt > p->max_pp) p->per_pkt = p->max_pp;
    if (p->link && p->rate_hz > 0)
        btlink_set_media_pace(p->link, (long)p->per_pkt * p->samples_per * 1000L / p->rate_hz);
}

static void packer_flush(packer *p)
{
    int bp;
    if (p->nframes <= 0) return;
    if (avdtp_send_media(p->av, p->buf, p->nbytes, p->samples_per * p->nframes,
                         p->nframes)) {
        p->pkts++;
        p->frames += p->nframes;
    }
    p->nbytes = p->nframes = 0;
    /* Between packets: adapt the bitpool to what the radio delivers. */
    bp = hb_rate_update(&p->rate, now_ms(), btlink_tx_backlog(p->link),
                        btlink_media_cap(p->link), btlink_tx_dropped(p->link));
    if (bp != sbc_encoder_bitpool(p->enc)) {
        int old = sbc_encoder_bitpool(p->enc), old_pp = p->per_pkt;
        sbc_encoder_set_bitpool(p->enc, bp);
        packer_size(p);
        log_line("media: bitpool %d -> %d (queue %d, dropped %ld): %d-byte frames, %d -> %d frames/packet",
                 old, bp, btlink_tx_backlog(p->link), btlink_tx_dropped(p->link),
                 p->fsz, old_pp, p->per_pkt);
    }
}

/* Once a second: measure what this link really delivers (credits returned
 * and packets sent per second, drops) and tune the media queue depth,
 * frames per packet and the bitpool ceiling from it. Same logic for every
 * headset or speaker. */
static void tune_link(packer *p, long now)
{
    static unsigned long l_sent, l_cred;
    static long l_drop, l_t, t_jit_ok;
    static int jitter_s;
    unsigned long sent = 0, cred = 0;
    long drops = btlink_tx_dropped(p->link), dt;
    int limit = 0, pkt_ms, cap, fit;
    double need_pps, cred_pps;

    btlink_tx_counters(p->link, &sent, &cred, &limit);
    if (!l_t || sent < l_sent) {                  /* new link */
        l_sent = sent; l_cred = cred; l_drop = drops; l_t = now; t_jit_ok = now;
        jitter_s = 0;
        return;
    }
    dt = now - l_t;
    if (dt < 900) return;
    pkt_ms = p->rate_hz > 0 ? p->per_pkt * p->samples_per * 1000 / p->rate_hz : 25;
    if (pkt_ms < 1) pkt_ms = 1;
    need_pps = 1000.0 / pkt_ms;
    cred_pps = (double)(cred - l_cred) * 1000.0 / (double)dt;

    /* Queue: about one second of audio, whatever the packet size. */
    cap = (1000 + pkt_ms - 1) / pkt_ms;
    if (cap < 16) cap = 16;
    if (cap != btlink_media_cap(p->link)) {
        btlink_set_media_cap(p->link, cap);
        log_line("tune: media queue %d packets (~1 s at %d ms/packet)", btlink_media_cap(p->link), pkt_ms);
    }

    if (drops > l_drop && (cred_pps >= need_pps * 1.05 ||
                           btlink_tx_backlog(p->link) * 2 > btlink_media_cap(p->link))) {
        /* Enough credits on average but still dropping: bursty credit
         * return. Smaller packets hold each credit for less time. */
        if (++jitter_s >= 2 && p->per_pkt > 5) {
            p->max_pp = p->per_pkt - 2 < 5 ? 5 : p->per_pkt - 2;
            packer_size(p);
            log_line("tune: drops with backlog %d (%.0f credits/s for %.0f packets/s): %d frames/packet",
                     btlink_tx_backlog(p->link),
                     cred_pps, need_pps, p->per_pkt);
            jitter_s = 0;
        }
        t_jit_ok = now;
    } else if (drops == l_drop) {
        jitter_s = 0;
        (void)fit; (void)t_jit_ok;
    }
    /* Bitpool: only step up while the link returns credits with headroom. */
    if (cred_pps < need_pps * 1.15 && btlink_tx_backlog(p->link) > 1)
        p->rate.t_calm = now;
    l_sent = sent; l_cred = cred; l_drop = drops; l_t = now;
}

/* Encode pcm[frames] and send full packets. Returns 0 on encode error. */
static int packer_feed(packer *p, const int16_t *pcm, int frames)
{
    int off = 0;
    while (off < frames) {
        int chunk = frames - off, got;
        if (chunk > p->samples_per) chunk = p->samples_per;
        got = sbc_encoder_encode(p->enc, pcm + off * 2, chunk, p->buf + p->nbytes,
                                 sizeof p->buf - (size_t)p->nbytes);
        off += chunk;
        if (got < 0) {
            log_line("media: SBC encode error");
            return 0;
        }
        if (got == 0) continue;
        if (p->nframes && HB_MEDIA_HDR + p->nbytes + got > p->mtu) {
            /* Never exceed the MTU, whatever the frame count says: the new
             * frame starts the next packet. */
            unsigned char keep[HCI_PKT_MAX];
            memcpy(keep, p->buf + p->nbytes, (size_t)got);
            packer_flush(p);
            memcpy(p->buf, keep, (size_t)got);
        }
        p->nbytes += got;
        p->nframes += got / p->fsz > 0 ? got / p->fsz : 1;
        if (p->nframes >= p->per_pkt ||
            p->nbytes + p->fsz > (int)sizeof p->buf)
            packer_flush(p);
    }
    return 1;
}

enum { RUN_DROPPED = 0, RUN_STOP = 1, RUN_FAIL = 2, RUN_PAUSED = 3, RUN_SWITCH = 4 };

/* One connection: select/connect → AVDTP → SBC → capture → stream until
 * the stop file or a link drop. */
static int run_session(a2dp_session *asess, hci_t hci, headset_ini *ini)
{
    btlink *link = NULL;
    avdtp_session av;
    sbc_encoder *enc = NULL;
    avcap2_session *cap = NULL;
    sbc_config scfg;
    packer pk;
    unsigned av_psm = 0, mtu;
    int16_t pcm[PCM_CAP_FRAMES * 2];
    int rc = RUN_FAIL, r = 0;
    long t_start, t_stat, samples = 0, reads_ok = 0, reads_empty = 0;
    double tone_phase = 0.0, sine_phase = 0.0;
    float peak_seen = 0.f;
    int tone = 0, tone_file = 0, gain_milli = 1000, out_peak = 0, gain_pct, muted;
    int hs_vol = -1, avst = 0;

    memset(&av, 0, sizeof av);
    {
        /* Manual only: act on what the user pressed, one attempt, no
         * background paging. Nothing pressed (start-up, after a drop):
         * stay idle, or show the scan list when nothing is saved yet. */
        hb_cmd pend = g_pending;
        int user = g_user_connect;
        memset(&g_pending, 0, sizeof g_pending);
        if (pend.kind == CMD_ADDR || pend.kind == CMD_INDEX) {
            r = try_pick(asess, hci, &pend, NULL, NULL, 0, ini, &link, &av_psm);
        } else if (pend.kind == CMD_RECONNECT || (user && pend.kind == CMD_NONE)) {
            if (ini->ok) {
                hold_clear(ini->addr);
                r = try_saved(hci, ini, &link, &av_psm, 30000);
                if (r != 1)
                    write_status("error %s %s", conn_fail_label(0), ini->name[0] ? ini->name : "-");
            } else if (g_npaired) {
                headset_ini first = g_paired[0];        /* most recent saved device */
                first.ok = first.have_addr = 1;
                hold_clear(first.addr);
                r = try_saved(hci, &first, &link, &av_psm, 30000);
                if (r == 1) {
                    *ini = first;
                    keep_identity(ini);
            if (!headset_ini_save(ini)) log_line("saved: cannot write headset.ini");
                    publish_saved(ini);
                } else {
                    write_status("error %s %s", conn_fail_label(0), first.name[0] ? first.name : "-");
                }
            }
        }
        g_user_connect = 0;
        if (hb_stop_requested()) { rc = RUN_STOP; goto done; }
        if (r != 1) {
            /* Not connected: keep the device list fresh (inquiry only, no
             * paging) and wait for the user's choice. A failed attempt
             * keeps its error label. */
            if (!discover_and_select(asess, hci, ini, &link, &av_psm, pend.kind == CMD_SCAN)) {
                log_line("select: no A2DP sink found / paired");
                goto done;
            }
        }
    }
    log_line("stream: encrypted ACL ready, AVDTP PSM %#x", av_psm);

    {
        int av_ok = avdtp_setup(&av, link, av_psm);
        if (!av_ok && g_kept_link && !hb_stop_requested()) {
            /* Kept pairing link: AVDTP failed there — close, page, retry once. */
            log_line("stream: AVDTP on the pairing link failed — closing it and paging");
            btlink_disconnect(link);
            btlink_destroy(link);
            link = NULL;
            memset(&av, 0, sizeof av);
            {
                long w = now_ms() + 1000;
                while (now_ms() < w) idle_pump(hci, 50);
            }
            g_user_connect = 1;
            g_av_fail_ms = now_ms();
            av_ok = connect_and_probe(hci, ini, &link, &av_psm, 30000) == 1 &&
                    avdtp_setup(&av, link, av_psm);
            g_user_connect = 0;
        }
        g_kept_link = 0;
        if (!av_ok) {
            g_av_fail_ms = now_ms();
            log_line("stream: AVDTP setup failed");
            write_status("error avdtp");
            goto done;
        }
    }

    memset(&scfg, 0, sizeof scfg);
    scfg.sample_rate = av.sink.sample_rate ? av.sink.sample_rate : 48000;
    scfg.channels = av.sink.channels ? av.sink.channels : 2;
    scfg.bitpool = av.bitpool;
    scfg.blocks = 16;
    scfg.subbands = 8;
    scfg.allocation = 0;
    scfg.joint_stereo = av.sink.joint_stereo;
    memcpy(scfg.a2dp_ie, av.sbc_cfg, 4);
    scfg.have_a2dp_ie = 1;
    enc = sbc_encoder_open(&scfg);
    if (!enc) {
        log_line("stream: SBC encoder open failed");
        goto done;
    }
    if (scfg.sample_rate != 48000)
        log_line("stream: WARN sink %d Hz, capture is 48 kHz (no resample yet)",
                 scfg.sample_rate);

    cap = avcap2_session_open();
    if (!cap) {
        log_line("stream: Avcap2 open failed");
        goto done;
    }

    memset(&pk, 0, sizeof pk);
    pk.av = &av;
    pk.enc = enc;
    pk.link = link;
    pk.samples_per = sbc_encoder_frame_samples(enc);
    if (pk.samples_per <= 0) pk.samples_per = 128;
    mtu = btlink_chan_peer_mtu(link, av.media_scid);
    if (!mtu) mtu = 672;
    pk.mtu = (int)mtu;
    pk.rate_hz = scfg.sample_rate;
    packer_size(&pk);
    hb_rate_init(&pk.rate, av.bitpool_lo ? av.bitpool_lo : av.bitpool,
                 av.bitpool_hi ? av.bitpool_hi : av.bitpool, sbc_encoder_bitpool(enc), now_ms());
    log_line("stream: media MTU %u, SBC frame %d bytes, %d frames/packet, bitpool %d "
             "(adapts %d-%d)", mtu, pk.fsz, pk.per_pkt, sbc_encoder_bitpool(enc),
             pk.rate.lo, pk.rate.hi);

    if (avdtp_dump_open(&av, DUMP_PATH, DUMP_PKTS))
        log_line("stream: dumping first %d media payloads to %s", DUMP_PKTS, DUMP_PATH);
    else
        log_line("stream: media dump %s not writable", DUMP_PATH);
    tone_file = file_exists(TONE_PATH);

    /* AVRCP: most headsets open the control channel themselves right after
     * the stream starts; if not within ~1.5 s, open it ourselves. */
    {
        long w = now_ms() + 1500;
        while (now_ms() < w && !(btlink_avrcp_state(link) & 1))
            if (btlink_pump(link, 20) < 0) break;
        if (!(btlink_avrcp_state(link) & 1)) {
            log_line("avrcp: headset did not open AVRCP — opening it");
            if (!btlink_avrcp_connect(link))
                log_line("avrcp: control channel open failed (software gain only)");
        }
    }

    CTL_LOCK(&g_ctl);
    gain_pct = g_ctl.gain_pct;
    muted = g_ctl.muted;
    tone = g_ctl.tone || tone_file;
    g_ctl.sample_rate = scfg.sample_rate;
    g_ctl.bitpool = av.bitpool;
    CTL_UNLOCK(&g_ctl);
    log_line("stream: base gain %d%%%s%s", gain_pct, muted ? ", muted" : "",
             tone ? ", TEST TONE 1 kHz -6 dB" : "");

    write_status("connected %s", ini->name[0] ? ini->name : "-");
    ctl_set_state("streaming", ini->name);
    notify("hearbridge: connected %s", ini->name[0] ? ini->name : "headphones");
    log_line("stream: streaming until stop file or link drop");

    t_start = t_stat = now_ms();
    for (;;) {
        int nframes, req_vol = -1, req_disc = 0, changed = 0;
        float peak = 0.f;
        long now, ahead_ms;

        if (hb_stop_requested()) { rc = RUN_STOP; break; }
        if (btlink_pump(link, 1) < 0 || !btlink_is_up(link)) {
            log_line("stream: link dropped");
            rc = RUN_DROPPED;
            break;
        }
        if (av.remote_closed) {
            log_line("stream: headset closed the stream");
            rc = RUN_DROPPED;
            break;
        }
        if (!btlink_chan_is_open(link, av.media_scid)) {
            log_line("stream: headset closed the media channel");
            rc = RUN_DROPPED;
            break;
        }
        if (btlink_ms_since_credit(link) > 4000) {
            log_line("stream: no packet acknowledged for 4 s — link lost");
            rc = RUN_DROPPED;
            break;
        }

        /* Web requests + headset volume (cheap; under the lock). */
        {
            int v = btlink_avrcp_volume(link, &changed);
            avst = btlink_avrcp_state(link);
            CTL_LOCK(&g_ctl);
            req_vol = g_ctl.req_hs_volume;
            g_ctl.req_hs_volume = -1;
            req_disc = g_ctl.req_disconnect;
            g_ctl.req_disconnect = 0;
            if (changed || (avst & 1)) g_ctl.hs_volume = (avst & 3) ? v : -1;
            if (req_vol >= 0) g_ctl.hs_volume = req_vol;
            hs_vol = (avst & 2) || req_vol >= 0 ? g_ctl.hs_volume : -1;
            gain_pct = g_ctl.gain_pct;
            muted = g_ctl.muted;
            tone = g_ctl.tone || tone_file;
            g_ctl.avrcp = avst;
            CTL_UNLOCK(&g_ctl);
            if (changed) log_line("stream: headset volume %d/127 -> gain", v);
            if (req_vol >= 0) btlink_avrcp_set_volume(link, req_vol);
            gain_milli = ctl_effective_gain_milli(gain_pct, muted, hs_vol);
        }
        if (req_disc) {
            log_line("stream: Disconnect requested from the web page");
            rc = RUN_PAUSED;
            break;
        }

        /* Real-time pacing: never run more than ~40 ms ahead of the audio
         * clock (1024 frames at 48 kHz = 21.3 ms). */
        now = now_ms();
        ahead_ms = samples * 1000L / scfg.sample_rate - (now - t_start);
        if (ahead_ms > 40) {
            (void)btlink_pump(link, (int)(ahead_ms - 40 > 10 ? 10 : ahead_ms - 40));
            continue;
        }
        if (ahead_ms < -300) {           /* fell behind (stall): resync */
            t_start = now - samples * 1000L / scfg.sample_rate;
        }

        nframes = avcap2_session_read_s16(cap, pcm, PCM_CAP_FRAMES, &peak);
        if (nframes > 0) {
            reads_ok++;
            if (peak > peak_seen) peak_seen = peak;
        } else {
            /* No capture data: keep the clock running with silence, only
             * once we are behind the audio clock. */
            if (nframes == 0) reads_empty++;
            if (ahead_ms > 0) { usleep(1000); continue; }
            nframes = PCM_CAP_FRAMES / 4;      /* silence keeps the sink fed */
            fill_tone_s16(pcm, nframes, scfg.sample_rate, &tone_phase, 0.f);
        }
        if (tone) {
            /* Tone is fixed -6 dB; only mute applies. */
            fill_sine_1k(pcm, nframes, scfg.sample_rate, &sine_phase);
            if (muted) memset(pcm, 0, (size_t)nframes * 4);
            out_peak = muted ? 0 : 500;
        } else {
            int op = gain_apply_soft(pcm, nframes * 2, gain_milli);
            if (op > out_peak) out_peak = op;
        }
        if (!packer_feed(&pk, pcm, nframes)) break;
        samples += nframes;

        if (now - t_stat >= 1000) {
            log_line("stream: pkts=%ld sbc=%ld reads ok=%ld empty=%ld peak=%.4f "
                     "out=%.3f gain=%.2f hs=%d backlog=%d bitpool=%d frames/pkt=%d (mtu %d, frame %d B) dropped=%ld",
                     pk.pkts, pk.frames,
                     reads_ok, reads_empty, (double)peak_seen, out_peak / 1000.0,
                     gain_milli / 1000.0, hs_vol, btlink_tx_backlog(link),
                     sbc_encoder_bitpool(enc), pk.per_pkt, pk.mtu, pk.fsz, btlink_tx_dropped(link));
            CTL_LOCK(&g_ctl);
            g_ctl.pkts = pk.pkts;
            g_ctl.frames = pk.frames;
            g_ctl.empty_reads = reads_empty;
            g_ctl.peak_milli = (int)(peak_seen * 1000.f);
            g_ctl.out_peak_milli = out_peak;
            g_ctl.backlog = btlink_tx_backlog(link);
            g_ctl.bitpool = sbc_encoder_bitpool(enc);
            g_ctl.per_packet = pk.per_pkt;
            g_ctl.dropped = btlink_tx_dropped(link);
            g_ctl.bitpool_lo = pk.rate.lo;
            g_ctl.bitpool_hi = pk.rate.hi;
            g_ctl.uptime_s = (now - t_start) / 1000;
            CTL_UNLOCK(&g_ctl);
            tune_link(&pk, now);
            peak_seen = 0.f;
            out_peak = 0;
            t_stat = now;
            if (tone_file != file_exists(TONE_PATH)) {
                tone_file = !tone_file;
                log_line("stream: tone file %s", tone_file ? "present" : "removed");
            }
            persist_gain_if_dirty();
            {
                hb_cmd c;
                if (poll_cmd(&c, ini)) {
                    int same = c.kind == CMD_ADDR && !memcmp(c.addr, ini->addr, 6);
                    if (!same && c.kind != CMD_NONE) {
                        log_line("stream: switching on request from the page — closing the current headset first");
                        if (c.kind == CMD_ADDR) want_device(c.addr);
                        g_pending = c;
                        rc = RUN_SWITCH;
                        break;
                    }
                }
            }
        }
    }
    log_line("stream: ended — pkts=%ld sbc=%ld reads ok=%ld empty=%ld",
             pk.pkts, pk.frames, reads_ok, reads_empty);
    if (rc == RUN_DROPPED) {
        write_status("disconnected waiting for %s", ini->name[0] ? ini->name : "-");
        ctl_set_state("disconnected", ini->name);
    }

done:
    if (cap) avcap2_session_close(cap);
    if (enc) sbc_encoder_close(enc);
    if (av.link) avdtp_teardown(&av);
    if (link) {
        btlink_disconnect(link);
        if (rc == RUN_SWITCH) {
            /* Only one headset at a time: wait for the old link to be gone. */
            log_line("switch: old headset %s", btlink_last_close_confirmed()
                     ? "disconnected (confirmed)" : "close not confirmed — waiting 1 s");
            if (!btlink_last_close_confirmed()) usleep(1000 * 1000);
        }
        btlink_destroy(link);
    }
    if (rc == RUN_SWITCH) {
        write_status("disconnected %s", ini->name[0] ? ini->name : "-");
        publish_saved(NULL);
        ctl_set_state("connecting", NULL);
    }
    if (rc != RUN_STOP && hb_stop_requested()) rc = RUN_STOP;
    return rc;
}

int main(void)
{
    hci_t hci;
    a2dp_session *asess = NULL;
    a2dp_open_opts opts;
    headset_ini ini;
    int rc = 2;

    mkdir(STATE_DIR, 0755);
    unlink(DEVICES_JSON);      /* a list from an older run or build is stale */

    if (!lock_take(LOCK_PATH)) {
        notify("HearBridge: already running");
        return 1;
    }

    log_open(LOG_PATH);
    hb_stop_init();
    log_line("HearBridge PS5 %s", HEARBRIDGE_VERSION);
    log_line("attach to running controller (no reset); stop file %s", HB_STOP_PATH);
    ctl_init(&g_ctl, HEARBRIDGE_VERSION);
    snprintf(g_ctl.devices_path, sizeof g_ctl.devices_path, "%s", DEVICES_JSON);
    snprintf(g_ctl.select_path, sizeof g_ctl.select_path, "%s", SELECT_TXT);
    snprintf(g_ctl.saved_path, sizeof g_ctl.saved_path, "%s", SAVED_JSON);
    g_ctl.gain_pct = read_gain_pct();
    log_line("volume: base gain %d%% (%s)", g_ctl.gain_pct, GAIN_PATH);
    {
        char url[64];
        int port;
        a2dp_inquiry_abort = cmd_waiting;
        a2dp_inquiry_progress = inquiry_progress;
        btlink_abort_connect = btlink_forget_abort;
        btlink_on_acl_up = on_acl_up;
        port = http_start(&g_ctl, url, (int)sizeof url);
        if (port) {
            CTL_LOCK(&g_ctl);
            snprintf(g_ctl.url, sizeof g_ctl.url, "%s", url);
            CTL_UNLOCK(&g_ctl);
            log_line("http: control page at %s", url);
            notify("HearBridge %s: %s", HEARBRIDGE_VERSION, url);
        } else {
            log_line("http: control page could not start");
            notify("hearbridge: started\n%s", HEARBRIDGE_VERSION);
        }
    }

    /* Home-screen tile that opens the control page in the browser. */
    if (access(RM_TILE_PATH, F_OK) == 0) {
        tile_uninstall();
        unlink(RM_TILE_PATH);
        { FILE *f = fopen(NO_TILE_PATH, "w"); if (f) fclose(f); }
    } else if (access(NO_TILE_PATH, F_OK) != 0) {
        char u[200] = "";
        FILE *f = fopen(TILE_URL_PATH, "r");
        if (f) {
            if (!fgets(u, sizeof u, f)) u[0] = 0;
            fclose(f);
            u[strcspn(u, "\r\n ")] = 0;
            if (!strcmp(u, "start")) snprintf(u, sizeof u, "%s", HB_TILE_START_URL);
        }
        tile_install(u);
    }

    if (!headset_ini_load(&ini) && !ini.have_addr)
        log_line("select: no headset.ini — will discover a new device");
    g_npaired = paired_load(PAIRED_INI, g_paired, PAIRED_MAX);
    if (ini.ok && paired_find(g_paired, g_npaired, ini.addr) < 0) {
        g_paired[g_npaired < PAIRED_MAX ? g_npaired++ : PAIRED_MAX - 1] = ini;
        paired_save(PAIRED_INI, g_paired, g_npaired);
    }
    if (!ini.ok && g_npaired) use_saved(0, &ini);
    log_line("saved: %d paired device(s)%s", g_npaired, ini.ok ? ", reconnecting the current one first" : "");
    publish_saved(&ini);

    memset(&hci, 0, sizeof hci);
    if (!hci_usb_open(&hci)) {
        log_line("hearbridge: HCI open failed");
        notify("HearBridge: HCI open failed");
        goto out;
    }

    memset(&opts, 0, sizeof opts);
    opts.inquiry_seconds = 10;
    if (ini.have_addr) memcpy(opts.prefer_addr, ini.addr, 6);
    asess = a2dp_open(hci, &opts);
    if (!asess) {
        log_line("hearbridge: controller setup failed");
        notify("HearBridge: controller setup failed");
        goto close_hci;
    }

    /* Stream until stopped; reconnect after a drop or failure. The web
     * page can pause (Disconnect) and resume (Connect). */
    for (;;) {
        int r = run_session(asess, hci, &ini);
        int paused;
        persist_gain_if_dirty();
        if (r == RUN_STOP) { rc = 0; break; }
        if (r == RUN_SWITCH) continue;
        CTL_LOCK(&g_ctl);
        paused = g_ctl.paused;
        g_ctl.req_connect = 0;
        CTL_UNLOCK(&g_ctl);
        /* Manual only: idle until the user presses Connect (or picks a
         * device / Scan). No background paging, no auto-reconnect. */
        if ((r == RUN_PAUSED || paused) && ini.ok) hold_add(ini.addr);
        if (r == RUN_DROPPED) {
            g_want_until = 0;
            notify("HearBridge: connection lost");
        }
        publish_saved(NULL);
        if (r == RUN_PAUSED || paused || r == RUN_DROPPED || !g_ctl.detail[0] ||
            strncmp(g_ctl.detail, "error", 5)) {
            write_status("disconnected");
            ctl_set_state("disconnected", NULL);
        } else {
            ctl_set_state("error", NULL);     /* keep the error label (e.g. page-timeout) */
        }
        CTL_LOCK(&g_ctl);
        g_ctl.device[0] = 0;
        CTL_UNLOCK(&g_ctl);
        log_line("hearbridge: idle — waiting for Connect");
        {
            for (;;) {
                int go;
                if (hb_stop_requested()) break;
                CTL_LOCK(&g_ctl);
                go = g_ctl.req_connect;
                g_ctl.req_connect = 0;
                if (go) g_ctl.paused = 0;
                CTL_UNLOCK(&g_ctl);
                if (go && !g_pending.kind) {
                    (void)poll_cmd(&g_pending, &ini);   /* a pick from the page wins */
                    if (!g_pending.kind) g_nhold = 0;   /* plain Connect: the current one again */
                }
                if (!go && poll_cmd(&g_pending, &ini)) go = 1;
                if (go) {
                    CTL_LOCK(&g_ctl);
                    g_ctl.paused = 0;
                    CTL_UNLOCK(&g_ctl);
                    g_user_connect = 1;
                    break;
                }
                persist_gain_if_dirty();
                idle_pump(hci, 100);
            }
        }
        if (hb_stop_requested()) { rc = 0; break; }
        (void)headset_ini_load(&ini);
        continue;
    }

    a2dp_close(asess);
close_hci:
    if (hci.ops && hci.ops->close) hci.ops->close(hci.ctx);
out:
    http_stop();
    persist_gain_if_dirty();
    {
        int cc = btlink_last_close_confirmed();
        log_line("stop: own ACL close %s%s",
                 cc == 1 ? "confirmed (Disconnection Complete)" :
                 cc == 0 ? "sent, completion NOT seen" : "not needed (no link)",
                 btlink_own_acl_pending() ? "; WARNING: an own handle is still marked up" : "");
    }
    if (hb_stop_requested()) {
        write_status("stopped");
        log_line("stop: clean exit");
    }
    hb_stop_clear();
    log_close();
    lock_release();
    return rc;
}
