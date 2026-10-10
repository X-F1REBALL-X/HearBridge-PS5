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
#include "diag.h"
#include "sysinfo.h"
#include "hci_cmd.h"
#include "hci_usb.h"
#include "hcidbg.h"
#include "btchip.h"
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
#include "hsprefs.h"
#include "eq.h"
#include "cswitch.h"
#include "rejoin.h"
#include "forgot.h"
#include "connreq.h"
#include "http.h"
#include "linkq.h"
#include "alerts.h"
#include "avrcp.h"
#include "night.h"
#include "linktune.h"
#include "rest_sys.h"
#include "gameprof.h"
#include "game_sys.h"
#include "backup.h"

#include <pthread.h>
#include <time.h>

#include <sys/stat.h>

#include <errno.h>

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
#define DUMP_FLAG_PATH STATE_DIR "/media_dump"   /* exists → dump the first media packets (debug) */
#define NO_TILE_PATH STATE_DIR "/no_tile"      /* exists → never add the home tile */
#define RM_TILE_PATH STATE_DIR "/remove_tile"  /* exists → remove the tile once */
#define TILE_URL_PATH STATE_DIR "/tile_url"    /* optional: deep link for the tile ("start" = fallback page) */
#define DIAG_PATH STATE_DIR "/diag.txt"        /* diagnostics report, also at /api/diag */
#define HCI_DEBUG_PATH STATE_DIR "/hci_debug"  /* exists → /api/hcilog trace + /api/hci raw commands */
#define CHIP_PATH STATE_DIR "/chip"            /* optional: "mediatek" or "marvell" forces the chip profile */
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
/* 1 during a background page nobody pressed for (rejoin after a drop, the
 * idle auto page): another saved headset calling in wins over it. */
static int g_bg_page;
static void note_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_why(const char *key);
static void ctl_set_state(const char *st, const char *dev);
static int probe_link(btlink *link, headset_ini *ini, btlink **linkp, unsigned *psm);
static void idle_pump(hci_t hci, int ms);
static int g_kept_link;
static unsigned char g_target[6];        /* device being paged / paired / used */
static int g_have_target;
static int set_target(const unsigned char *a);
static btlink *g_ready;          /* headset already connected in (gentle rejoin) */
static unsigned g_ready_psm;
static long g_av_fail_ms;   /* last AVDTP failure: the headset needs a moment */     /* the current link is the kept pairing ACL */

/* Idle: keep reading HCI events (link tracking sees a headset that
 * connects by itself) instead of sleeping; nothing is answered here. */
static void idle_pump(hci_t hci, int ms)
{
    unsigned char ev[HCI_PKT_MAX];
    hcidbg_service(hci);
    if (!hci.ops || hci.ops->pump(hci.ctx, ms) < 0) { usleep((useconds_t)ms * 1000); return; }
    while (hci.ops->next_event(hci.ctx, ev, (int)sizeof ev) > 0) { }
    while (hci.ops->next_acl(hci.ctx, ev, (int)sizeof ev) > 0) { }
}

/* The Bluetooth USB device is gone (rest mode resets it) and it was not a
 * Stop: everything has to be reopened. */
static int transport_dead(hci_t hci)
{
    if (!hci.ops || hb_stop_requested()) return 0;
    return hci.ops->pump(hci.ctx, 0) < 0 && !hb_stop_requested();
}

static long wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Console slept since the last call (rest mode)? Checked by every idle loop. */
static long g_tick_mono, g_tick_wall;
static int woke_up(void)
{
    long m = now_ms(), w = wall_ms();
    int gap = hb_resume_gap(g_tick_mono, m, g_tick_wall, w, HB_WAKE_GAP_MS);
    g_tick_mono = m;
    g_tick_wall = w;
    return gap;
}

static void bg_tick(headset_ini *ini);

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

/* How long a page may take. A headset that is there answers in a
 * second or two; this only bounds a miss. Long enough that a slow page
 * still finishes, short enough that we do not sit for half a minute. */
#define HB_PAGE_MS 5000
#define HB_DROPPED_LISTEN_MS 10000   /* page to a just-left headset failed: listen this long */
#define HB_VOLQ_MS     15000   /* page open, no volume report this long: ask the headset again */
#define HB_VOLQ_PAGE_MS 5000   /* page counts as open while it polled within this */
#define HB_BATT_WAIT_MS 8000   /* no battery report by then: "Not shown by this headset" */
/* After a hang-up right after encryption: wait this long for its own call. */
#define HB_CALLBACK_MS 2500
static int probe_after(hci_t hci, btlink *link, headset_ini *ini, btlink **linkp, unsigned *psm);
static void turn_down_other_calls(hci_t hci, const unsigned char target[6]);

static int connect_and_probe(hci_t hci, headset_ini *ini, btlink **linkp,
                             unsigned *psm, int timeout_ms)
{
    btlink *link = NULL;

    *linkp = NULL;
    if (connect_abort(ini->addr)) {
        log_line("select: connect aborted for a page command");
        return 0;
    }
    if (!g_user_connect && g_av_fail_ms && now_ms() - g_av_fail_ms < 2500) {
        long w = g_av_fail_ms + 2500;
        log_line("select: waiting %ld ms after the last AVDTP failure", w - now_ms());
        while (now_ms() < w) idle_pump(hci, 50);
    }
    link = btlink_create(hci, 1021, 7);
    if (!link) return 0;
    turn_down_other_calls(hci, ini->addr);   /* nothing else holds the radio */
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
        } else if (age >= 0 && age < ACL_REQ_PENDING_MS) {
            /* Its own call is still waiting: take it (a page now fails 0x0b). */
            if (accept_one(link, ini, 2000)) return probe_after(hci, link, ini, linkp, psm);
        }
    }
    if (!btlink_connect(link, ini->addr, 0x01, 0, ini->link_key, ini->key_type,
                        ini->name, (int)sizeof ini->name, timeout_ms)) {
        int ok = 0;
        if (btlink_last_connect_fail() == 0x04 && !connect_abort(ini->addr)) {
            /* Page timeout: the device may connect in on power-up. Listen
             * for its own request during this Connect only. */
            log_line("select: page timeout — listening briefly for the device to connect in");
            ok = accept_one(link, ini, 2000);
        } else if (!connect_abort(ini->addr) &&
                   (btlink_last_connect_fail() == 0x0B || btlink_last_connect_fail() == 0)) {
            unsigned h = acl_track_handle(ini->addr);
            if (h) {
                /* 0x0b: a link exists after all — close that handle, page once more. */
                log_line("select: 0x0b — closing the existing link %#05x and paging again", h);
                (void)btlink_drop_handle(link, h, 2500);
                ok = btlink_connect(link, ini->addr, 0x01, 0, ini->link_key, ini->key_type,
                                    ini->name, (int)sizeof ini->name, timeout_ms);
            } else {
                /* No handle of ours. The page was cancelled in btlink.
                 * Do not guess a handle (that drops a pad) and do not sit. */
                long age = acl_track_request_age(ini->addr, now_ms());
                log_line("select: 0x0b, no headset ACL of ours — page cancelled");
                if (age >= 0 && age < ACL_REQ_PENDING_MS) {
                    log_line("select: it is calling us (%ld ms ago) — accepting that instead", age);
                    ok = accept_one(link, ini, 2000);
                }
            }
        }
        if (!ok) {
            btlink_destroy(link);
            return 0;
        }
    }
    if (!link) return 0;
    return probe_after(hci, link, ini, linkp, psm);
}

/* probe_link, and when a saved headset hangs up right after encryption,
 * give it a moment to call back before anyone pages it again. The Xbox
 * headset drops a link we opened (0x13, ~15 ms after its Device ID lookup)
 * and calls the console itself 0.2-1.1 s later (16 log); accepted, that
 * call streams (12 log). */
static int probe_after(hci_t hci, btlink *link, headset_ini *ini, btlink **linkp, unsigned *psm)
{
    int pr = probe_link(link, ini, linkp, psm);
    if (pr == -2 && ini->ok && !connect_abort(ini->addr)) {
        btlink *in;
        log_line("select: waiting %d ms for %s to call back", HB_CALLBACK_MS,
                 ini->name[0] ? ini->name : "the headset");
        if (g_user_connect)
            note_event("%s hung up, waiting for it to call back", ini->name[0] ? ini->name : "The headset");
        in = btlink_create(hci, 1021, 7);
        if (!in) return pr;
        if (!accept_one(in, ini, HB_CALLBACK_MS)) {
            log_line("select: no call back");
            btlink_destroy(in);
            return pr;
        }
        pr = probe_link(in, ini, linkp, psm);   /* once: no loop */
    }
    return pr;
}

/* SDP check on an encrypted link; on success the link is handed out. */
static int probe_link(btlink *link, headset_ini *ini, btlink **linkp, unsigned *psm)
{
    int sdp;
    {
        /* Short look for channels the headset opens itself; a link that
         * dropped meanwhile fails at once instead of SDP/AVDTP on a dead link. */
        /* A headset that called us usually opens SDP, then AVDTP itself:
         * give it 1.5 s before we open AVDTP on its link ourselves. */
        long w = now_ms() + (btlink_is_incoming(link) ? 1500 : 150);
        while (now_ms() < w) {
            if (btlink_pump(link, 30) < 0) break;
            if (!btlink_is_up(link) || btlink_chan_find_inbound(link, BTLINK_PSM_AVDTP)) break;
        }
        if (!btlink_is_up(link)) {
            log_line("select: headset dropped the link right after encryption (reason %#04x)",
                     btlink_last_disc_reason());
            btlink_destroy(link);
            return -2;
        }
    }
    note_event("Connected securely");
    if (ini->ok && (btlink_chan_find_inbound(link, BTLINK_PSM_AVDTP) ||
                    btlink_chan_find_inbound(link, BTLINK_PSM_SDP))) {
        /* A saved audio device that opened SDP/AVDTP to us itself: no need
         * for our own SDP on top of its channels (it can stall them). */
        log_line("select: headset opened its own channels — skipping our SDP");
        *psm = BTLINK_PSM_AVDTP;
        *linkp = link;
        return 1;
    }
    sdp = sdp_probe_a2dp_sink(link, 6000, psm);
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
#define SELECT_WAIT_S 86400 /* manual mode: wait for a choice indefinitely */
/* A scan (button or page refresh) runs inquiries back to back for
 * SCAN_WINDOW_S. Each result is written to devices.json as it arrives.
 * The list stays until the next scan. */
#define SCAN_WINDOW_S 12   /* short: the radio is shared with the DualSense */

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
    a2dp_rank_log_reset();
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
enum { CMD_NONE, CMD_ADDR, CMD_INDEX, CMD_SCAN, CMD_RECONNECT, CMD_FORGET_CUR };
typedef struct { int kind, index, user; unsigned char addr[6]; } hb_cmd;   /* user: Scan button */

static headset_ini g_paired[PAIRED_MAX];
static int g_npaired;
static hb_cmd g_pending;              /* command that ended the last session */
static hb_cswitch g_cs;               /* codec switch that needs a reconnect */

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
static unsigned char g_hold_man[PAIRED_MAX];   /* 1: Disconnect pressed for it */
static int g_nhold;

static long g_hold_ms;     /* when the last hold was set */
static int hold_idx(const unsigned char a[6])
{
    int i;
    for (i = 0; i < g_nhold; i++) if (!memcmp(g_hold[i], a, 6)) return i;
    return -1;
}
static int held(const unsigned char a[6])
{
    return hold_idx(a) >= 0;
}

/* Kept out of auto-accept and background pages right now. A manual
 * Disconnect holds it until the user picks it again (Connect, Reconnect,
 * a press on its row); a switch to another headset only for 30 s. */
static int held_now(const unsigned char a[6], long now)
{
    int i = hold_idx(a);
    return i >= 0 && (g_hold_man[i] || now - g_hold_ms < 30000);
}

static void hold_add(const unsigned char a[6], int manual)
{
    int i = hold_idx(a);
    g_hold_ms = now_ms();
    if (i >= 0) { if (manual) g_hold_man[i] = 1; return; }
    if (g_nhold >= PAIRED_MAX) return;
    g_hold_man[g_nhold] = (unsigned char)(manual != 0);
    memcpy(g_hold[g_nhold++], a, 6);
    log_line("saved: auto-reconnect paused for the disconnected device%s",
             manual ? " (until it is picked again)" : "");
}

/* The saved headset we disconnected last (switch / Disconnect) and when.
 * Its callback right after is accepted and closed cleanly, never turned
 * down busy (the Xbox Wireless Headset stops answering pages after that
 * until power cycled). */
static hb_dropped g_dropped;
static struct { unsigned char addr[6]; long t; int on; } g_accdrop;

/* Accept a just-left headset's call and close it with 0x13 once its ACL is
 * up (see the conn-req tick). */
static void accept_drop(hci_t hci, const unsigned char a[6])
{
    btlink_accept_request(hci, a, 1);
    memcpy(g_accdrop.addr, a, 6);
    g_accdrop.t = now_ms();
    g_accdrop.on = 1;
}

static void hold_clear(const unsigned char a[6])
{
    int i;
    for (i = 0; i < g_nhold; i++)
        if (!memcmp(g_hold[i], a, 6)) {
            memmove(g_hold[i], g_hold[i + 1], (size_t)(g_nhold - i - 1) * 6);
            memmove(g_hold_man + i, g_hold_man + i + 1, (size_t)(g_nhold - i - 1));
            g_nhold--;
            return;
        }
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
    hb_forgot_clear(d->addr);
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

/* Another saved headset (not addr, not one just disconnected by hand) has
 * a call waiting at the controller: the user took it out of its case. */
static int other_saved_calling(const unsigned char addr[6])
{
    int i;
    long now = now_ms();
    for (i = 0; i < g_npaired; i++) {
        const unsigned char *a = g_paired[i].addr;
        long age;
        if (!memcmp(a, addr, 6)) continue;
        if (held_now(a, now)) continue;
        age = acl_track_request_age(a, now);
        if (age >= 0 && age < ACL_REQ_PENDING_MS && !acl_track_handle(a)) {
            static long logged;
            if (now - logged > 2000) {
                logged = now;
                log_line("select: another saved headset is calling in — stopping the background page for it");
            }
            return 1;
        }
    }
    return 0;
}

/* Stop the page / listen in progress for addr: a background attempt yields
 * to any page command; a user attempt yields to a Forget of it, a pick of
 * another device, Scan or Reconnect. */
static int g_rejoining;                    /* gentle_rejoin runs for g_rejoin_addr */
static unsigned char g_rejoin_addr[6];
static int connect_abort(const unsigned char addr[6])
{
    FILE *f;
    char line[64];
    unsigned char a[6];
    int stop = 1, reset = 0, disc = 0;
    CTL_LOCK(&g_ctl);
    reset = g_ctl.req_reset;
    disc = g_ctl.req_disconnect;
    CTL_UNLOCK(&g_ctl);
    if (reset) return 1;                 /* drop our page, not anyone else's */
    if (disc) return 1;                  /* Disconnect pressed while it was connecting */
    if (!g_user_connect && (g_bg_page || (g_rejoining && !memcmp(addr, g_rejoin_addr, 6)))) {
        /* Disconnect pressed: no background page or rejoin of it goes on */
        int off;
        CTL_LOCK(&g_ctl);
        off = g_ctl.paused || g_ctl.req_disconnect;
        CTL_UNLOCK(&g_ctl);
        if (off) return 1;
    }
    if (g_bg_page && other_saved_calling(addr)) return 1;   /* taken out of its case: it wins */
    if (!cmd_waiting()) return 0;
    if (!g_user_connect) return 1;
    f = fopen(SELECT_TXT, "r");
    if (!f) return 0;
    if (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "forget ", 7))
            stop = headset_parse_addr(line + 7, a) && !memcmp(a, addr, 6);
        else if (!strcmp(line, "reconnect"))
            stop = 1;
        else if (!strcmp(line, "scan"))
            stop = 0;                    /* a refresh scan must not cancel this page */
        else if (!strcmp(line, "scanu"))
            stop = 1;                    /* the Scan button: the user wants the list */
        else if (headset_parse_addr(line, a))
            stop = 1;                    /* a new press starts again, same headset too */
    }
    fclose(f);
    return stop;
}

static int btlink_forget_abort(const unsigned char addr[6])
{
    return connect_abort(addr);
}

/* The waiting page command is a Connect / Reconnect that this headset
 * answers (its own address, or Reconnect while it is a saved one). */
static int press_is_for(const unsigned char addr[6])
{
    FILE *f;
    char line[64];
    unsigned char a[6];
    int yes = 0;
    if (!cmd_waiting()) return 0;
    f = fopen(SELECT_TXT, "r");
    if (!f) return 0;
    if (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "reconnect")) yes = paired_find(g_paired, g_npaired, addr) >= 0;
        else if (strncmp(line, "forget ", 7) && headset_parse_addr(line, a)) yes = !memcmp(a, addr, 6);
    }
    fclose(f);
    return yes;
}

/* A saved device answered a page or connected in: show "connecting" now. */
static void on_acl_up(const unsigned char addr[6])
{
    int i = paired_find(g_paired, g_npaired, addr);
    const char *nm = i >= 0 && g_paired[i].name[0] ? g_paired[i].name : "-";
    note_event("Connecting to %s…", i >= 0 && g_paired[i].name[0] ? nm : "the headset");
    write_status("connecting %s", nm);
    ctl_set_state("connecting", i >= 0 ? g_paired[i].name : NULL);
}

static int g_stream_up;            /* run_session is streaming to ini */
/* Rest mode: the watcher asks the stream to stop (g_rest_stop), the stream
 * leaves g_rest_resume set, the watcher or the idle loop's own clock check
 * sees the wake (g_rest_wake) and the idle loop connects again. */
static volatile int g_rest_stop, g_rest_resume, g_rest_wake;
static void prefs_forget(const unsigned char addr[6]);

/* Deletes everything saved for addr: key (paired.ini, headset.ini when it
 * is the current one), its settings file, its saved.json row. It is then
 * not accepted or paged again until it is paired again. */
static void forget_device(const unsigned char addr[6], headset_ini *ini, int cur)
{
    if (paired_drop(g_paired, &g_npaired, addr)) paired_save(PAIRED_INI, g_paired, g_npaired);
    hold_clear(addr);
    if (!memcmp(g_want, addr, 6)) g_want_until = 0;
    prefs_forget(addr);
    hb_forgot_add(addr);
    acl_track_request_clear(addr);
    if (cur || (ini->ok && !memcmp(ini->addr, addr, 6))) {
        unlink(HEADSET_INI_PATH);
        memset(ini, 0, sizeof *ini);
    }
    publish_saved(ini);
}

/* Reads and deletes select.txt. "forget" is handled here; forgetting the
 * current headset turns into a scan (while streaming: CMD_FORGET_CUR, the
 * stream disconnects cleanly first and deletes after). */
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
    else if (!strcmp(line, "scanu")) { c->kind = CMD_SCAN; c->user = 1; }
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
        if (hb_forget_action(cur, g_stream_up) == HB_FORGET_AFTER_DISCONNECT) {
            log_line("saved: Forget of the connected headset — disconnecting it first");
            c->kind = CMD_FORGET_CUR;
            log_line("select: page command received: \"%s\"", line);
            return 1;
        }
        log_line("saved: forgot a device%s", cur || shown ? " (the current one)" : "");
        forget_device(c->addr, ini, cur);
        if (cur || shown) {
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
    if (r == -1) return "not-a2dp-sink";
    if (btlink_last_connect_fail() == 0x04) return "page-timeout";
    if (btlink_last_connect_fail() == 0x0B) return "link-held-elsewhere";
    return "connect-failed";
}
static void set_why(const char *key);
static void ctl_set_state(const char *st, const char *dev);

/* Pair the chosen device (saves headset.ini), reconnect, SDP-check. */
static int try_device_(a2dp_session *asess, hci_t hci, const a2dp_inq_dev *d,
                       headset_ini *ini, btlink **linkp, unsigned *psm);
static int try_device(a2dp_session *asess, hci_t hci, const a2dp_inq_dev *d,
                      headset_ini *ini, btlink **linkp, unsigned *psm)
{
    unsigned char prev[6];
    int had, r;
    memcpy(prev, g_target, 6);
    had = set_target(d->addr);
    r = try_device_(asess, hci, d, ini, linkp, psm);
    set_target(had ? prev : NULL);
    return r;
}

static int try_device_(a2dp_session *asess, hci_t hci, const a2dp_inq_dev *d,
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
            if (pr.need_pair_mode)
                note_event("%s didn't pair. Put it in pairing mode (hold the pair button until it flashes fast) and try again.",
                           d->name[0] ? d->name : "The headset");
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
            log_line("select: headset closed the pairing link — listening briefly, then paging");
            btlink_destroy(k);
            k = btlink_create(hci, 1021, 7);          /* fresh state for the new link */
            if (k && accept_one(k, ini, 2000)) {
                int pr2 = probe_link(k, ini, linkp, psm);
                if (pr2 == 1) {
                    log_choice("chosen", ini);
                    write_status("connected %s", ini->name[0] ? ini->name : "-");
                    return 1;
                }
                if (pr2 == -1) { write_status("error not-a2dp-sink"); return 0; }
                if (pr2 < 0) return 0;                                /* k freed */  /* k freed */
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


/* Page scan: the headset connects in on its own (power on, out of the case).
 * 1 = encrypted link probed and handed out. */
static int listen_saved(hci_t hci, headset_ini *ini, btlink **linkp, unsigned *psm, int ms)
{
    btlink *link;
    int pr;
    if (!ini || !ini->ok || ms <= 0) return 0;
    link = btlink_create(hci, 1021, 7);
    if (!link) return 0;
    if (!accept_one(link, ini, ms)) {
        btlink_destroy(link);
        return 0;
    }
    if (press_is_for(ini->addr)) {
        /* The Connect press was for this headset, which joined by itself: done. */
        hb_cmd c;
        (void)poll_cmd(&c, ini);
        log_line("select: that press is answered by the incoming link");
    }
    pr = probe_link(link, ini, linkp, psm);
    return pr == 1;
}

/* Idle: accept any saved headset that connects in (power on, out of its
 * case). Never one the user disconnected by hand until it is picked again;
 * one left by a switch only after 30 s. 1 = link probed and handed out, *ini is
 * then that headset (saved as the current one). */
static int listen_any_saved(hci_t hci, headset_ini *ini, btlink **linkp, unsigned *psm, int ms)
{
    unsigned char a[8][6], k[8][16], kt[8];
    int idx[8], n = 0, i, which = -1, pr;
    btlink *link;
    headset_ini cand;
    for (i = 0; i < g_npaired && n < 8; i++) {
        if (held_now(g_paired[i].addr, now_ms())) continue;
        memcpy(a[n], g_paired[i].addr, 6);
        memcpy(k[n], g_paired[i].link_key, 16);
        kt[n] = g_paired[i].key_type;
        idx[n++] = i;
    }
    if (!n) {                               /* nobody to listen for: wait, but in */
        long end = now_ms() + ms;           /* short slices so a press is seen */
        while (now_ms() < end && !hb_stop_requested() && !cmd_waiting())
            idle_pump(hci, 50);
        return 0;
    }
    link = btlink_create(hci, 1021, 7);
    if (!link) return 0;
    if (!btlink_accept(link, (const unsigned char (*)[6])a, (const unsigned char (*)[16])k, kt, n,
                       ms, &which) || which < 0 || which >= n) {
        btlink_destroy(link);
        return 0;
    }
    cand = g_paired[idx[which]];
    cand.ok = cand.have_addr = 1;
    if (press_is_for(cand.addr)) {          /* a press for it is answered by this link */
        hb_cmd c;
        (void)poll_cmd(&c, &cand);
    }
    if (held(cand.addr)) {                  /* a switch hold that ran out: auto-connect is back */
        hold_clear(cand.addr);
        CTL_LOCK(&g_ctl);
        g_ctl.paused = 0;
        CTL_UNLOCK(&g_ctl);
    }
    note_event("%s turned on, connecting", cand.name[0] ? cand.name : "Headset");
    pr = probe_link(link, &cand, linkp, psm);
    if (pr != 1) return 0;
    *ini = cand;
    keep_identity(ini);
    if (!headset_ini_save(ini)) log_line("saved: cannot write headset.ini");
    remember_device(ini);
    publish_saved(ini);
    return 1;
}

static int saved_peer(const unsigned char addr[6])
{
    return paired_find(g_paired, g_npaired, addr) >= 0 || hb_forgot_has(addr);
}

/* Connecting to target: a call still waiting from another of our saved (or
 * forgotten) headsets, e.g. the one just disconnected calling back, is
 * turned down first so the controller is not busy with it. */
static void turn_down_other_calls(hci_t hci, const unsigned char target[6])
{
    int i;
    long now = now_ms();
    for (i = 0; i < g_npaired + hb_forgot_count(); i++) {
        const unsigned char *a = i < g_npaired ? g_paired[i].addr : hb_forgot_at(i - g_npaired);
        long age;
        if (!a || !memcmp(a, target, 6)) continue;
        age = acl_track_request_age(a, now);
        if (age >= 0 && age < ACL_REQ_PENDING_MS && !acl_track_handle(a)) {
            if (i < g_npaired && hb_dropped_recent(&g_dropped, a, now, HB_DROPPED_MS)) accept_drop(hci, a);
            else btlink_reject_request(hci, a, 0x0D);
        }
    }
}

/* --- every incoming connection request gets an answer ----------------- */
static unsigned char g_stream_addr[6];   /* valid while g_stream_up */
static volatile int g_switch_req;        /* a saved headset called while we stream */
static unsigned char g_switch_addr[6];

static int set_target(const unsigned char *a)
{
    int had = g_have_target;
    if (a) { memcpy(g_target, a, 6); g_have_target = 1; }
    else g_have_target = 0;
    return had;
}

static void conn_req_hook(hci_t hci, const unsigned char *ev, int n)
{
    long now = now_ms();
    if (ev && n >= 12) {
        const unsigned char *a = ev + 2;
        unsigned cod = (unsigned)ev[8] | ((unsigned)ev[9] << 8) | ((unsigned)ev[10] << 16);
        int si = paired_find(g_paired, g_npaired, a), d;
        hb_cr_in in;
        char astr[18];
        if (ev[11] != 0x01) return;                  /* SCO/eSCO: not ours */
        memset(&in, 0, sizeof in);
        in.is_av = hb_cod_is_av(cod);
        in.saved = si >= 0;
        in.forgotten = hb_forgot_has(a);
        in.is_target = g_have_target && !memcmp(a, g_target, 6);
        in.streaming_other = g_stream_up && memcmp(a, g_stream_addr, 6) != 0;
        in.busy_other = (g_have_target && !in.is_target) ||
                        (g_stream_up && !memcmp(a, g_stream_addr, 6));
        in.held = held(a) && (g_stream_up || held_now(a, now));
        in.bg_page = g_bg_page && g_have_target && !in.is_target && !g_stream_up;
        in.just_dropped = in.saved && hb_dropped_recent(&g_dropped, a, now, HB_DROPPED_MS);
        d = hb_connreq_decide(&in);
        hci_addr_str(a, astr);
        log_line("conn-req: %s CoD %06x%s%s -> %s", astr, cod,
                 in.saved ? " saved" : in.forgotten ? " forgotten" : "",
                 in.is_target ? " (target)" : "", hb_connreq_name(d));
        switch (d) {
        case HB_CR_REJECT_UNKNOWN: btlink_reject_request(hci, a, 0x0F); break;
        case HB_CR_REJECT_BUSY:    btlink_reject_request(hci, a, 0x0D); break;
        case HB_CR_ACCEPT_DROP:    accept_drop(hci, a); break;
        case HB_CR_SWITCH:
            memcpy(g_switch_addr, a, 6);
            g_switch_req = 1;
            break;
        default: break;                               /* TAKE: accepted by the state machine; LEAVE: system */
        }
        return;
    }
    {
        /* Safety net: a call from one of ours that nobody took within 5 s
         * is turned down (busy) before it blocks pages for ~25 s. */
        static long last;
        int i;
        if (g_accdrop.on) {
            /* A just-left headset we accepted: close it cleanly once up,
             * unless the user picked it again meanwhile. */
            unsigned h = acl_track_handle(g_accdrop.addr);
            if (g_have_target && !memcmp(g_target, g_accdrop.addr, 6)) {
                g_accdrop.on = 0;
                log_line("conn-req: the headset that called back was picked again, keeping it");
            } else if (h) {
                btlink_hci_disconnect(hci, h, 0x13);
                g_accdrop.on = 0;
            } else if (now - g_accdrop.t > 5000) {
                g_accdrop.on = 0;
                log_line("conn-req: the accepted call never came up");
            }
        }
        if (now - last < 500) return;
        last = now;
        for (i = 0; i < g_npaired + hb_forgot_count(); i++) {
            const unsigned char *a = i < g_npaired ? g_paired[i].addr : hb_forgot_at(i - g_npaired);
            long age;
            if (!a) continue;
            if (g_have_target && !memcmp(a, g_target, 6)) continue;
            if (g_switch_req && !memcmp(a, g_switch_addr, 6)) continue;
            age = acl_track_request_age(a, now);
            if (age < 5000 || age >= ACL_REQ_PENDING_MS || acl_track_handle(a)) continue;
            log_line("conn-req: nobody took a call for %ld ms -> turning it down", age);
            btlink_reject_request(hci, a, i < g_npaired ? 0x0D : 0x0F);
        }
    }
}

/* Never leave a saved headset's call unanswered (it blocks our pages to it
 * with 0x0b until the controller times it out, ~25 s). One the user just
 * disconnected by hand is turned down; any other is accepted and connected.
 * Other devices (pads) are left to the system. 1 = link handed out. */
static int answer_saved_calls(hci_t hci, headset_ini *ini, btlink **linkp, unsigned *psm)
{
    int i, take = 0;
    long now = now_ms();
    for (i = 0; i < hb_forgot_count(); i++) {        /* forgotten: not until paired again */
        const unsigned char *fa = hb_forgot_at(i);
        long age = acl_track_request_age(fa, now);
        if (age >= 0 && age < ACL_REQ_PENDING_MS && !acl_track_handle(fa))
            btlink_reject_request(hci, fa, 0x0F);
    }
    for (i = 0; i < g_npaired; i++) {
        long age = acl_track_request_age(g_paired[i].addr, now);
        if (age < 0 || age >= ACL_REQ_PENDING_MS || acl_track_handle(g_paired[i].addr)) continue;
        if (hb_dropped_recent(&g_dropped, g_paired[i].addr, now, HB_DROPPED_MS)) {
            accept_drop(hci, g_paired[i].addr);
            continue;
        }
        if (held_now(g_paired[i].addr, now)) {
            btlink_reject_request(hci, g_paired[i].addr, 0x0D);
            continue;
        }
        take = 1;
    }
    if (!take) return 0;
    return listen_any_saved(hci, ini, linkp, psm, 2500);
}

/* Reconnect with a saved key (no pairing). 1 = link ready. */
static int try_saved(hci_t hci, headset_ini *ini, btlink **linkp, unsigned *psm, int timeout_ms)
{
    int r;
    if (!ini->ok) return 0;
    log_choice("reconnecting saved", ini);
    if (g_user_connect) {
        note_event("Calling %s…", ini->name[0] ? ini->name : "the headset");
        write_status("connecting %s", ini->name[0] ? ini->name : "-");
        ctl_set_state("connecting", ini->name);
    } else {
        write_status("disconnected waiting for %s", ini->name[0] ? ini->name : "-");
        ctl_set_state("disconnected", ini->name);
    }
    {
        unsigned char prev[6];
        int had;
        memcpy(prev, g_target, 6);
        had = set_target(ini->addr);
        r = connect_and_probe(hci, ini, linkp, psm, timeout_ms);
        set_target(had ? prev : NULL);
    }
    if (r == 1) {
        remember_device(ini);
        if (press_is_for(ini->addr)) {        /* a second press of it: already done */
            hb_cmd c;
            (void)poll_cmd(&c, ini);
        }
        return 1;
    }
    log_line("select: saved device did not answer (%s)", conn_fail_label(r));
    if (g_user_connect && r != -1 && r != -2 &&
        hb_dropped_recent(&g_dropped, ini->addr, now_ms(), HB_DROPPED_PAGE_MS)) {
        /* We left it a moment ago and it does not answer pages: some
         * (Xbox Wireless Headset) only come back by calling in. Listen for
         * it a while, then ask for a power cycle. */
        const char *nm = ini->name[0] ? ini->name : "the headset";
        int ok;
        unsigned char prev[6];
        int had;
        log_line("select: %s left a moment ago, listening %d s for it to connect in", nm, HB_DROPPED_LISTEN_MS / 1000);
        memcpy(prev, g_target, 6);
        had = set_target(ini->addr);
        ok = listen_saved(hci, ini, linkp, psm, HB_DROPPED_LISTEN_MS);
        set_target(had ? prev : NULL);
        (void)woke_up();
        if (ok) {
            remember_device(ini);
            return 1;
        }
        if (cmd_waiting() || hb_stop_requested()) return 0;   /* stopped by a press, not a timeout */
        note_event("Turn %s off and on to reconnect", nm);
        set_why("powercycle");
        return 0;
    }
    if (g_user_connect) {
        const char *nm = ini->name[0] ? ini->name : "headset";
        int f = btlink_last_connect_fail();
        if (r == -1) note_event("%s is not an audio headset", nm);
        else if (r == -2) note_event("%s hung up right after connecting", nm);
        else if (f == 0x04) note_event("%s did not answer. Is it off, in its case or too far?", nm);
        else if (f == 0x0B) note_event("%s is busy. Try again in a few seconds", nm);
        else note_event("Could not connect to %s", nm);
    }
    if (btlink_last_connect_fail() == 0x04) set_why("timeout");
    else if (btlink_last_connect_fail() == 0x0B) set_why("held");
    else set_why("failed");
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
            if (!g_user_connect) {
                log_line("rotation: skip \"%s\" (disconnected by hand)", cand.name);
                continue;
            }
            hold_clear(cand.addr);       /* Reconnect was pressed: page it anyway */
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
        hb_forgot_clear(c->addr);
        int s = paired_find(g_paired, g_npaired, c->addr);
        if (s >= 0) {                              /* already paired: use the key */
            use_saved(s, ini);
            if (try_saved(hci, ini, linkp, psm, HB_PAGE_MS)) return 1;   /* one attempt */
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
        a2dp_scan_evmask_sent = 0;
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
        int disc;
        if (hb_stop_requested()) return 0;
        if (transport_dead(hci)) {
            log_line("select: Bluetooth device went away");
            return 0;
        }
        CTL_LOCK(&g_ctl);
        disc = g_ctl.req_disconnect;
        g_ctl.req_disconnect = 0;
        CTL_UNLOCK(&g_ctl);
        if (disc) {                       /* Disconnect pressed: stop, stay idle */
            log_line("select: stopped by Disconnect");
            return 0;
        }
        bg_tick(ini);
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
                    if (try_saved(hci, &cand, linkp, psm, HB_PAGE_MS)) {
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
                a2dp_scan_evmask_sent = 0;
                scan_until = now_ms() + SCAN_WINDOW_S * 1000L;
        a2dp_scan_deadline_ms = scan_until;
                continue;
            }
            g_user_connect = 1;
            if (c.kind == CMD_RECONNECT) ok = try_all_saved(hci, ini, linkp, psm, HB_PAGE_MS, HB_PAGE_MS);
            else ok = try_pick(asess, hci, &c, found, order, ncand, ini, linkp, psm);
            if (ok) return 1;
            if (c.kind != CMD_ADDR) write_status("waiting-selection %d", ncand);
            else t_keep_err = now_ms();
        }
        if (!cmd_waiting()) {
            idle_pump(hci, 100);
            /* A saved headset calling in while the list is up (power on,
             * or the Xbox calling back after it hung up on our page). */
            if (answer_saved_calls(hci, ini, linkp, psm)) return 1;
        }
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

static void set_why(const char *key)
{
    CTL_LOCK(&g_ctl);
    snprintf(g_ctl.why, sizeof g_ctl.why, "%s", key ? key : "");
    CTL_UNLOCK(&g_ctl);
}

static void ctl_set_state(const char *st, const char *dev)
{
    CTL_LOCK(&g_ctl);
    snprintf(g_ctl.state, sizeof g_ctl.state, "%s", st);
    if (!strcmp(st, "streaming")) g_ctl.why[0] = 0;
    if (dev) snprintf(g_ctl.device, sizeof g_ctl.device, "%s", dev);
    if (strcmp(st, "streaming")) ctl_clear_link(&g_ctl, !strcmp(st, "paused"));
    CTL_UNLOCK(&g_ctl);
}

/* Base gain percent from GAIN_PATH; default 250 (middle) when absent. */
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


/* Per-headset settings (hsprefs.h) of the headset in use. */
static hb_prefs g_prefs;
static unsigned char g_prefs_addr[6];
static int g_prefs_have;
/* Per-game profile in use (gameprof.h): "" none. While one is on, page
 * edits of EQ / boost / headset volume stay live (Save for this game keeps
 * them) and do not overwrite the headset's own settings. */
static hb_games g_games;
static char g_game_applied[16];
static hb_game g_game_on;       /* copy of the profile on now (valid while g_game_applied[0]) */
static void games_migrate_locked(void);

/* Page values -> g_prefs. Caller holds the lock. */
static void prefs_from_ctl(void)
{
    g_prefs.codec = g_ctl.codec_pref;
    g_prefs.latency_ms = g_ctl.latency_ms;
    if (g_game_applied[0]) return;     /* EQ and night belong to the game profile now */
    g_prefs.night = g_ctl.night;
    g_prefs.eq_on = g_ctl.eq_on;
    memcpy(g_prefs.eq_db, g_ctl.eq_db, sizeof g_prefs.eq_db);
    /* gain is not copied: only a slider move / Clean sound sets it
     * (persist_gain_if_dirty), so no other headset's gain lands here. */
}

/* Page values <- g_prefs. Caller holds the lock. */
static void prefs_to_ctl(void)
{
    g_ctl.codec_pref = g_prefs.codec;
    g_ctl.latency_ms = hb_latency_clamp(g_prefs.latency_ms);
    g_ctl.night = g_prefs.night;
    g_ctl.eq_on = g_prefs.eq_on;
    memcpy(g_ctl.eq_db, g_prefs.eq_db, sizeof g_ctl.eq_db);
    g_ctl.eq_seq++;
    g_ctl.gain_pct = hb_prefs_gain(&g_prefs);   /* the user's, else 250 */
}

static void persist_gain_if_dirty(void);

static void prefs_forget(const unsigned char addr[6])
{
    char path[160];
    hb_prefs_path(HB_PREFS_DIR, addr, path, (int)sizeof path);
    if (unlink(path) == 0) log_line("prefs: settings of the forgotten headset deleted");
    if (g_prefs_have && !memcmp(addr, g_prefs_addr, 6)) g_prefs_have = 0;   /* nothing saved back */
}

static void prefs_save(void)
{
    if (!g_prefs_have) return;
    if (hb_prefs_save(HB_PREFS_DIR, g_prefs_addr, &g_prefs))
        log_line("prefs: saved for this headset (codec %s, latency %d ms, EQ %s %d/%d/%d/%d/%d dB)",
                 hb_codec_key(g_prefs.codec), g_prefs.latency_ms, g_prefs.eq_on ? "on" : "off", g_prefs.eq_db[0], g_prefs.eq_db[1], g_prefs.eq_db[2],
                 g_prefs.eq_db[3], g_prefs.eq_db[4]);
    else
        log_line("prefs: cannot write %s", HB_PREFS_DIR);
}

/* A headset is connecting: its own settings, or (first time) what the page
 * shows now, which then becomes its settings. */
static void prefs_attach(const unsigned char addr[6])
{
    hb_prefs p;
    hb_prefs_default(&p);
    if (g_prefs_have && !memcmp(addr, g_prefs_addr, 6)) return;
    persist_gain_if_dirty();          /* a pending slider move belongs to the previous one */
    memcpy(g_prefs_addr, addr, 6);
    g_prefs_have = 1;
    CTL_LOCK(&g_ctl);
    games_migrate_locked();            /* profiles from before per-headset: this headset's */
    CTL_UNLOCK(&g_ctl);
    if (hb_prefs_load(HB_PREFS_DIR, addr, &p)) {
        g_prefs = p;
        CTL_LOCK(&g_ctl);
        prefs_to_ctl();
        g_ctl.prefs_dirty = 0;
        CTL_UNLOCK(&g_ctl);
        log_line("prefs: loaded for this headset (codec %s, latency %d ms, gain %d%%%s)",
                 hb_codec_key(g_prefs.codec), g_prefs.latency_ms, hb_prefs_gain(&g_prefs),
                 g_prefs.gain_user ? " set by you" : " default");
    } else {
        g_prefs = p;
        CTL_LOCK(&g_ctl);
        prefs_from_ctl();
        /* No file yet: 200 ms, even if the page was still on the old 1 s mode.
         * A value already saved above is left alone. */
        hb_prefs_new_headset(&g_prefs);
        g_ctl.latency_ms = g_prefs.latency_ms;
        g_ctl.gain_pct = hb_prefs_gain(&g_prefs);   /* not set for it yet: 250 */
        CTL_UNLOCK(&g_ctl);
        prefs_save();
    }
}

/* Gain and per-headset settings (codec, latency, EQ) changed from the page:
 * write them down. */
static void persist_gain_if_dirty(void)
{
    int pct = -1, prefs = 0;
    CTL_LOCK(&g_ctl);
    if (g_ctl.gain_dirty) {
        pct = g_ctl.gain_pct;
        g_ctl.gain_dirty = 0;
        if (g_prefs_have && !g_game_applied[0]) {   /* the user moved it for this headset */
            g_prefs.gain_pct = pct;
            g_prefs.gain_user = 1;
            prefs = 1;
        }
    }
    if (g_ctl.prefs_dirty) { prefs_from_ctl(); prefs = 1; g_ctl.prefs_dirty = 0; }
    CTL_UNLOCK(&g_ctl);
    if (prefs) prefs_save();
    if (pct >= 0) {
        write_gain_pct(pct);
        log_line("volume: base gain %d%% saved", pct);
    }
}

static void note_event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- per-game profiles -------------------------------------------- */

static void games_load(void)
{
    static char buf[HB_GPROF_MAX * 200];
    FILE *f = fopen(HB_GAMES_PATH, "r");
    size_t n = 0;
    if (f) {
        n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
    }
    buf[n] = 0;
    hb_games_parse(&g_games, buf);
    games_migrate_locked();
}

static void games_save(void)
{
    static char buf[HB_GPROF_MAX * 200];
    int n = hb_games_format(&g_games, buf, (int)sizeof buf);
    FILE *f;
    if (n <= 0 || !(f = fopen(HB_GAMES_PATH ".tmp", "w"))) {
        log_line("game: cannot write %s", HB_GAMES_PATH);
        return;
    }
    fwrite(buf, 1, (size_t)n, f);
    fclose(f);
    rename(HB_GAMES_PATH ".tmp", HB_GAMES_PATH);
}

/* Profiles saved before they were per headset (no "hs=") belong to the
 * headset in use, once one is known. Caller holds the lock. */
static int g_games_dirty;      /* games.txt to be written (outside the lock) */
static void games_migrate_locked(void)
{
    int n;
    if (!g_prefs_have) return;
    n = hb_games_migrate(&g_games, g_prefs_addr);
    if (n) {
        log_line("game: %d profile(s) from before per-headset profiles moved to this headset", n);
        g_games_dirty = 1;
    }
}

/* Saved games for the page's Games list, with the headsets each one has a
 * profile for. Caller holds the lock. */
static void games_publish_locked(void)
{
    int idx[HB_GAME_MAX], n = hb_games_list(&g_games, idx, HB_GAME_MAX), i, j;
    g_ctl.games_n = n < 32 ? n : 32;
    for (i = 0; i < g_ctl.games_n; i++) {
        const hb_game *top = &g_games.g[idx[i]];
        int k = 0;
        snprintf(g_ctl.games_id[i], sizeof g_ctl.games_id[i], "%s", top->id);
        g_ctl.games_name[i][0] = 0;
        for (j = 0; j < g_games.n && k < 4; j++) {
            const hb_game *g = &g_games.g[j];
            int pi;
            if (strcmp(g->id, top->id)) continue;
            if (!g_ctl.games_name[i][0] && g->name[0])
                snprintf(g_ctl.games_name[i], sizeof g_ctl.games_name[i], "%s", g->name);
            if (!g->has_hs) continue;
            memcpy(g_ctl.games_hs[i][k], g->hs, 6);
            pi = paired_find(g_paired, g_npaired, g->hs);
            snprintf(g_ctl.games_hsname[i][k], sizeof g_ctl.games_hsname[i][k], "%s",
                     pi >= 0 && g_paired[pi].name[0] ? g_paired[pi].name : "");
            g_ctl.games_hs_cur[i][k] = g_prefs_have && !memcmp(g->hs, g_prefs_addr, 6);
            k++;
        }
        g_ctl.games_hs_n[i] = k;
    }
}

/* The headset's own EQ / boost / night / volume back (game closed or
 * forgotten). Caller holds the lock. */
static void game_restore_locked(void)
{
    if (!g_prefs_have) return;
    g_ctl.eq_on = g_prefs.eq_on;
    memcpy(g_ctl.eq_db, g_prefs.eq_db, sizeof g_ctl.eq_db);
    g_ctl.eq_seq++;
    g_ctl.gain_pct = hb_prefs_gain(&g_prefs);
    g_ctl.night = g_prefs.night;
    g_ctl.req_hs_volume = hb_prefs_hs_volume(&g_prefs);
}

/* A profile onto the live sound. Caller holds the lock. */
static void game_apply_locked(const hb_game *g)
{
    g_ctl.eq_on = g->eq_on;
    memcpy(g_ctl.eq_db, g->eq_db, sizeof g_ctl.eq_db);
    g_ctl.eq_seq++;
    g_ctl.gain_pct = g->gain_pct;
    if (g->night >= 0) g_ctl.night = g->night;
    if (g->hs_vol >= 0) g_ctl.req_hs_volume = g->hs_vol;
    g_game_on = *g;
    snprintf(g_game_applied, sizeof g_game_applied, "%s", g->id);
}

static void hs_label(const unsigned char a[6], char *out, int max)
{
    int pi = paired_find(g_paired, g_npaired, a);
    if (pi >= 0 && g_paired[pi].name[0]) snprintf(out, (size_t)max, "%s", g_paired[pi].name);
    else snprintf(out, (size_t)max, "%02X:%02X:%02X", a[3], a[4], a[5]);
}

/* Running game changed, or Save / Update / Remove on the page. Once a
 * second. Profiles are per game and per headset: the headset in use gets
 * its own, else the game's newest from another headset (used, not copied:
 * Update Game Profile saves it for this one). */
static void game_tick(void)
{
    char id[16], name[HB_GAME_NAME], drop[16], dname[HB_GAME_NAME], hsg[16], hsn[40], from[32];
    unsigned char hsa[6];
    const unsigned char *cur;
    int req, act, gi, exact = 0, dropped = 0, hsdo, hs_dropped = 0, picked = 0, own_dropped = 0, save;
    CTL_LOCK(&g_ctl);
    cur = g_prefs_have ? g_prefs_addr : NULL;
    snprintf(drop, sizeof drop, "%s", g_ctl.req_game_drop);
    g_ctl.req_game_drop[0] = 0;
    dname[0] = 0;
    if (drop[0] && (gi = hb_games_find(&g_games, drop)) >= 0) {
        snprintf(dname, sizeof dname, "%s", g_games.g[gi].name);
        hb_games_drop(&g_games, drop);
        if (!strcmp(g_game_applied, drop)) game_restore_locked();
        if (!strcmp(g_game_applied, drop)) g_game_applied[0] = 0;
        dropped = 1;
    }
    /* one headset's profile: forget it, or use it now */
    snprintf(hsg, sizeof hsg, "%s", g_ctl.req_game_hs);
    memcpy(hsa, g_ctl.req_game_hs_addr, 6);
    hsdo = g_ctl.req_game_hs_do;
    g_ctl.req_game_hs[0] = 0;
    g_ctl.req_game_hs_do = 0;
    hsn[0] = 0;
    if (hsdo && hsg[0]) hs_label(hsa, hsn, (int)sizeof hsn);
    if (hsdo == 1 && hsg[0] && (gi = hb_games_find_hs(&g_games, hsg, hsa)) >= 0) {
        int was_on = !strcmp(g_game_applied, hsg) && g_game_on.has_hs && !memcmp(g_game_on.hs, hsa, 6);
        snprintf(dname, sizeof dname, "%s", g_games.g[gi].name);
        hb_games_drop_hs(&g_games, hsg, hsa);
        if (was_on) g_game_applied[0] = 0;   /* the next one (or the usual sound) below */
        if (was_on && hb_games_find(&g_games, hsg) < 0) game_restore_locked();
        hs_dropped = 1;
    } else if (hsdo == 2 && hsg[0] && !strcmp(hsg, g_ctl.game_id) &&
               (gi = hb_games_find_hs(&g_games, hsg, hsa)) >= 0) {
        hb_game pick = g_games.g[gi];
        game_apply_locked(&pick);
        picked = 1;
    }
    snprintf(id, sizeof id, "%s", g_ctl.game_id);
    snprintf(name, sizeof name, "%s", g_ctl.game_name);
    req = g_ctl.req_game;
    g_ctl.req_game = 0;
    if (req == 1 && id[0]) {
        hb_game g;
        memset(&g, 0, sizeof g);
        snprintf(g.id, sizeof g.id, "%s", id);
        snprintf(g.name, sizeof g.name, "%s", name);
        g.eq_on = g_ctl.eq_on;
        memcpy(g.eq_db, g_ctl.eq_db, sizeof g.eq_db);
        g.gain_pct = g_ctl.gain_pct;
        g.hs_vol = g_ctl.hs_volume;
        g.night = g_ctl.night ? 1 : 0;
        if (cur) { g.has_hs = 1; memcpy(g.hs, cur, 6); }
        hb_games_put(&g_games, &g);
        g_game_on = g;
        snprintf(g_game_applied, sizeof g_game_applied, "%s", id);
    } else if (req == 2 && id[0]) {
        /* Remove: only this headset's own profile. A headset borrowing
         * another headset's profile has nothing to remove here, and the
         * other headsets keep theirs (the per-headset chips delete those). */
        int was = !strcmp(g_game_applied, id);
        own_dropped = cur && hb_games_drop_hs(&g_games, id, cur);
        if (own_dropped && was) g_game_applied[0] = 0;
        if (own_dropped && was && hb_games_find(&g_games, id) < 0) game_restore_locked();
    }
    gi = hb_games_pick(&g_games, id, cur, &exact);
    act = picked ? HB_GAME_KEEP : hb_game_decide(g_game_applied, id, gi >= 0);
    if (act == HB_GAME_APPLY) {
        hb_game g = g_games.g[gi];
        game_apply_locked(&g);
    } else if (act == HB_GAME_RESTORE) {
        game_restore_locked();
        g_game_applied[0] = 0;
    }
    g_ctl.game_profile = gi >= 0;
    g_ctl.game_active = g_game_applied[0] != 0;
    /* whose profile is on, and does the sound still match this headset's own */
    g_ctl.game_from[0] = 0;
    if (g_game_applied[0] && g_game_on.has_hs && !(cur && !memcmp(g_game_on.hs, cur, 6)))
        hs_label(g_game_on.hs, g_ctl.game_from, (int)sizeof g_ctl.game_from);
    {
        int own = id[0] ? hb_games_find_hs(&g_games, id, cur) : -1;
        g_ctl.game_exact = own >= 0 && g_game_applied[0] && !g_ctl.game_from[0];
        g_ctl.game_dirty = id[0] && (own < 0 ||
            hb_game_differs(&g_games.g[own], g_ctl.eq_on, g_ctl.eq_db, g_ctl.gain_pct, g_ctl.night));
    }
    games_publish_locked();
    snprintf(from, sizeof from, "%s", g_ctl.game_from);
    save = req || dropped || hs_dropped || g_games_dirty;
    g_games_dirty = 0;
    CTL_UNLOCK(&g_ctl);
    if (save) games_save();
    if (dropped) note_event("Game profile removed (%s)", dname[0] ? dname : drop);
    if (hs_dropped) note_event("Game profile removed (%s on %s)", dname[0] ? dname : hsg, hsn);
    if (picked) note_event("Using the %s profile of this game", hsn);
    if (req == 1) note_event("Saved for %s", name[0] ? name : id);
    if (req == 2 && own_dropped) note_event("Game profile removed (%s)", name[0] ? name : id);
    if (act == HB_GAME_APPLY && !req) {
        if (from[0]) note_event("Game sound on for %s (from %s)", name[0] ? name : id, from);
        else note_event("Game sound on for %s", name[0] ? name : id);
    }
    if (act == HB_GAME_RESTORE && !req) note_event("Game closed, your usual sound is back");
}

/* Polls the running game every 3 s (read only), off the stream loop. */
static pthread_t g_game_thr;
static int g_game_thr_up;
static volatile int g_game_quit;

/* Rest mode watcher: polls the system's rest request every 250 ms. */
static pthread_t g_rest_thr;
static int g_rest_thr_up;
static void *rest_thread(void *arg)
{
    hb_rest r;
    long pm = now_ms(), pw = wall_ms();
    (void)arg;
    hb_rest_init(&r);
    CTL_LOCK(&g_ctl);
    g_ctl.rest_watch = hb_rest_sys_avail();
    CTL_UNLOCK(&g_ctl);
    if (!g_ctl.rest_watch) return NULL;
    while (!g_game_quit && !hb_stop_requested()) {
        long m = now_ms(), w = wall_ms();
        int woke = hb_resume_gap(pm, m, pw, w, HB_WAKE_GAP_MS);
        int a = hb_rest_step(&r, hb_rest_sys_going_down(), woke, g_stream_up, m);
        pm = m;
        pw = w;
        if (a == HB_REST_STOP) {
            log_line("rest: console going to rest mode — stopping the stream");
            g_rest_stop = 1;
        } else if (a == HB_REST_RESUME) {
            log_line("rest: awake again");
            g_rest_wake = 1;
        }
        usleep(250 * 1000);
    }
    return NULL;
}
static void *game_thread(void *arg)
{
    char last[16] = "";
    (void)arg;
    CTL_LOCK(&g_ctl);
    g_ctl.game_avail = hb_game_sys_avail();
    CTL_UNLOCK(&g_ctl);
    if (!g_ctl.game_avail) return NULL;
    while (!g_game_quit && !hb_stop_requested()) {
        char id[16], name[HB_GAME_NAME] = "";
        int i;
        hb_game_sys_running(id, (int)sizeof id);
        if (strcmp(id, last)) {
            if (id[0]) {
                int gi;
                hb_game_sys_name(id, name, (int)sizeof name);
                CTL_LOCK(&g_ctl);
                gi = hb_games_find(&g_games, id);
                if (!name[0] && gi >= 0) snprintf(name, sizeof name, "%s", g_games.g[gi].name);
                CTL_UNLOCK(&g_ctl);
            }
            log_line("game: %s%s%s", id[0] ? id : "none", name[0] ? " " : "", name);
            CTL_LOCK(&g_ctl);
            snprintf(g_ctl.game_id, sizeof g_ctl.game_id, "%s", id);
            snprintf(g_ctl.game_name, sizeof g_ctl.game_name, "%s", name);
            CTL_UNLOCK(&g_ctl);
            snprintf(last, sizeof last, "%s", id);
        }
        for (i = 0; i < 12 && !g_game_quit; i++) usleep(250 * 1000);
    }
    return NULL;
}

/* ---- restore ------------------------------------------------------ */

/* The page restored a backup: read the saved headsets, their settings, the
 * gain and the game profiles again. The headset in use keeps playing. */
static void reload_settings(headset_ini *ini)
{
    int reload;
    CTL_LOCK(&g_ctl);
    reload = g_ctl.req_reload;
    g_ctl.req_reload = 0;
    CTL_UNLOCK(&g_ctl);
    if (!reload) return;
    g_npaired = paired_load(PAIRED_INI, g_paired, PAIRED_MAX);
    if (!g_stream_up) {
        headset_ini fresh;
        memset(&fresh, 0, sizeof fresh);
        if (headset_ini_load(&fresh) || fresh.have_addr) *ini = fresh;
        if (!ini->ok && g_npaired) use_saved(0, ini);
    }
    CTL_LOCK(&g_ctl);
    games_load();
    games_publish_locked();
    g_game_applied[0] = 0;               /* re-applied on the next tick */
    if (g_prefs_have) {
        hb_prefs p;
        hb_prefs_default(&p);
        if (hb_prefs_load(HB_PREFS_DIR, g_prefs_addr, &p)) {
            g_prefs = p;
            prefs_to_ctl();
        }
    } else {
        g_ctl.gain_pct = read_gain_pct();
    }
    CTL_UNLOCK(&g_ctl);
    publish_saved(g_stream_up ? ini : (ini->ok ? ini : NULL));
    log_line("restore: settings reloaded (%d saved headset(s))", g_npaired);
}

/* Game profile and restore checks for the loops that wait (idle, scan
 * list, rejoin), at most once a second. */
static void bg_tick(headset_ini *ini)
{
    static long last;
    long now = now_ms();
    if (now - last < 1000) return;
    last = now;
    reload_settings(ini);
    game_tick();
}

/* One line in the log and on the page (last few codec switches / drops). */
static void note_event(const char *fmt, ...)
{
    char buf[HB_EVENT_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log_line("%s", buf);
    ctl_event(&g_ctl, buf);
}

/* Codec change on the open link: the hb_cswitch in-place steps (asked
 * codec, then plain SBC). 1 = streaming again; 0 = the link is gone or the
 * headset refused both, g_cs then holds the reconnect steps. */
static int codec_switch_in_place(avdtp_session *av, btlink *link, int want, int no_xq)
{
    hb_cs_begin(&g_cs, want, no_xq);
    while (g_cs.step == HB_CS_INPLACE || g_cs.step == HB_CS_INPLACE_SBC) {
        int w = g_cs.step == HB_CS_INPLACE ? g_cs.want : HB_CODEC_SBC;
        int ok = avdtp_switch_codec(av, w, g_cs.no_xq);
        int up = btlink_is_up(link) && btlink_chan_is_open(link, av->sig_scid);
        if (!ok) log_line("switch: in-place %s failed (link %s)", hb_codec_key(w), up ? "up" : "gone");
        hb_cs_next(&g_cs, ok, up);
    }
    if (g_cs.step == HB_CS_DONE) {
        log_line("switch: now %s, headset stayed connected", av->codec.name ? av->codec.name : "SBC");
        note_event("Sound quality changed");
        g_cs.step = HB_CS_IDLE;
        return 1;
    }
    return 0;
}


/* Encode + send up to max_sbc frames from pcm[frames]. Returns packets sent. */
/* Packs SBC frames into media packets of up to per_pkt frames. */
typedef struct {
    avdtp_session *av;
    sbc_encoder *enc;
    btlink *link;
    hb_rate rate;
    int fsz, samples_per, per_pkt, mtu;
    hb_tune tune;        /* tuned frames/packet ceiling (tune.max_pp, 0 = MTU fit) */
    int queue_ms;        /* media queue target (latency slider), ms */
    long bl_sum, bl_n;   /* backlog samples since the last status (latency estimate) */
    int rate_hz;
    unsigned char buf[HCI_PKT_MAX];
    int nbytes, nframes;
    long pkts, frames;
} packer;

/* As many whole frames as the media MTU holds (RTP 12 + SBC header 1;
 * the NUM field is 4 bits, so at most 15), from the frame length at the
 * bitpool in use. */
/* Frames/packet without the tuner's ceiling: MTU fit, buffer, latency target. */
static int packer_fit(const packer *p)
{
    int pp = hb_frames_per_packet(p->mtu, p->fsz);       /* always MTU-capped */
    int lat_pp = hb_latency_frames_cap(p->queue_ms, p->rate_hz, p->samples_per);
    if (pp * p->fsz > (int)sizeof p->buf) pp = (int)sizeof p->buf / p->fsz;
    if (lat_pp > 0 && pp > lat_pp) pp = lat_pp;
    return pp < 1 ? 1 : pp;
}

static void packer_size(packer *p)
{
    p->fsz = (int)sbc_encoder_frame_bytes(p->enc);
    if (p->fsz <= 0) p->fsz = 29;
    p->per_pkt = packer_fit(p);
    if (p->tune.max_pp > 0 && p->per_pkt > p->tune.max_pp) p->per_pkt = p->tune.max_pp;
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
    p->bl_sum += btlink_tx_backlog(p->link);
    p->bl_n++;
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
    static long l_drop, l_t;
    unsigned long sent = 0, cred = 0;
    long drops = btlink_tx_dropped(p->link), dt;
    int limit = 0, pkt_ms, cap, act;
    double need_pps, cred_pps;
    hb_tune_in in;

    btlink_tx_counters(p->link, &sent, &cred, &limit);
    if (!l_t || sent < l_sent) {                  /* new link */
        l_sent = sent; l_cred = cred; l_drop = drops; l_t = now;
        hb_tune_init(&p->tune);
        return;
    }
    dt = now - l_t;
    if (dt < 900) return;
    pkt_ms = p->rate_hz > 0 ? p->per_pkt * p->samples_per * 1000 / p->rate_hz : 25;
    if (pkt_ms < 1) pkt_ms = 1;
    need_pps = 1000.0 / pkt_ms;
    cred_pps = (double)(cred - l_cred) * 1000.0 / (double)dt;

    /* Queue: about queue_ms of audio (low latency ~200 ms, stable ~1 s),
     * whatever the packet size. */
    cap = hb_media_queue_cap(pkt_ms, p->queue_ms);
    if (cap != btlink_media_cap(p->link)) {
        btlink_set_media_cap(p->link, cap);
        log_line("tune: media queue %d packets (~%d ms at %d ms/packet)", btlink_media_cap(p->link),
                 btlink_media_cap(p->link) * pkt_ms, pkt_ms);
    }

    /* Frames per packet from the credits this link returns (linktune.h). */
    memset(&in, 0, sizeof in);
    in.now = now;
    in.per_pkt = p->per_pkt;
    in.fit_pp = packer_fit(p);
    in.cred_pps = cred_pps;
    in.need_pps = need_pps;
    in.new_drops = drops > l_drop ? (int)(drops - l_drop) : 0;
    in.backlog = btlink_tx_backlog(p->link);
    in.cap = btlink_media_cap(p->link);
    act = hb_tune_step(&p->tune, &in);
    if (act == HB_TUNE_DIP) {
        int old = sbc_encoder_bitpool(p->enc);
        hb_rate_hold_floor(&p->rate, now, p->tune.freeze_until);
        if (p->rate.cur != old) {
            sbc_encoder_set_bitpool(p->enc, p->rate.cur);
            packer_size(p);
        }
        log_line("tune: %s for %d s (%ld dropped, backlog %d/%d, %.0f credits/s for %.0f packets/s, bitpool %d -> %d)",
                 hb_tune_name(act), HB_TUNE_FREEZE_MS / 1000, drops - l_drop, in.backlog, in.cap,
                 cred_pps, need_pps, old, p->rate.cur);
    } else if (act != HB_TUNE_KEEP) {
        int old_pp = p->per_pkt;
        packer_size(p);
        log_line("tune: %s, %.0f credits/s for %.0f packets/s, %ld dropped, backlog %d: %d -> %d frames/packet (fit %d)",
                 hb_tune_name(act), cred_pps, need_pps, drops - l_drop, in.backlog, old_pp, p->per_pkt, in.fit_pp);
    }
    /* Bitpool: only step up while the link returns credits with headroom. */
    /* (Paced packets waiting for their due time are not a backlog.) */
    if (cred_pps < need_pps * 1.15 && btlink_tx_backlog(p->link) > HB_RATE_SLACK)
        hb_rate_not_calm(&p->rate, now);
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

enum { RUN_DROPPED = 0, RUN_STOP = 1, RUN_FAIL = 2, RUN_PAUSED = 3, RUN_SWITCH = 4, RUN_AWAY = 5,
       RUN_FORGOT = 6, RUN_SWITCH_IN = 7 };

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
    hb_night night;
    hb_batt_alert balert;
    int rest_now = 0;
    int hs_vol = -1, avst = 0, want_codec = HB_CODEC_AUTO, xq_bad_s = 0, xq_low_s = 0, settle_s = 0;
    static hb_eq eq;
    unsigned eq_seq = 0;
    long xq_drops = 0;
    int lat_changed = 0;
    hb_latency lat;
    int cs_want = -1, cs_no_xq = 0, switched = 0;
    int vol_applied = 0, vol_auto = 0;   /* headset volume sent once on connect */
    unsigned char forget_addr[6];        /* RUN_FORGOT: delete after the clean disconnect */
    unsigned char switch_from[6];        /* RUN_SWITCH_IN: the headset we leave */
    hb_linkq lq;                         /* drops per minute for the link meter */
    hb_lat_backoff bo;                   /* low buffer target: step back on drops */
    int hs_dirty = 0, lat_n = 0;
    long lat_sum = 0;

    memset(&av, 0, sizeof av);
    {
        /* Manual only: act on what the user pressed, one attempt, no
         * background paging. Nothing pressed (start-up, after a drop):
         * stay idle, or show the scan list when nothing is saved yet. */
        hb_cmd pend = g_pending;
        int user = g_user_connect;
        memset(&g_pending, 0, sizeof g_pending);
        if (g_ready) {
            link = g_ready;
            av_psm = g_ready_psm;
            g_ready = NULL;
            r = 1;
        } else if (pend.kind == CMD_ADDR || pend.kind == CMD_INDEX) {
            r = try_pick(asess, hci, &pend, NULL, NULL, 0, ini, &link, &av_psm);
        } else if (pend.kind == CMD_RECONNECT || (user && pend.kind == CMD_NONE)) {
            int cs = pend.kind == CMD_RECONNECT && g_cs.step == HB_CS_RECONNECT;
            if (ini->ok) {
                hold_clear(ini->addr);
                /* Listen first. A page right after we dropped the link is
                 * ignored for minutes (Xbox); the headset connects in when
                 * it is ready. */
                if (cs) {
                    int left = g_cs.delay_ms > 0 ? (int)g_cs.delay_ms : 8000;
                    while (left > 0 && r != 1 && !hb_stop_requested() && !cmd_waiting()) {
                        int slice = left > 5000 ? 5000 : left;
                        r = listen_saved(hci, ini, &link, &av_psm, slice);
                        left -= slice;
                    }
                }
                if (r != 1)
                    r = try_saved(hci, ini, &link, &av_psm, HB_PAGE_MS);
                while (cs && r != 1 && !hb_stop_requested() && !cmd_waiting() &&
                       hb_cs_next(&g_cs, 0, 0) == HB_CS_RECONNECT) {
                    int left = g_cs.delay_ms > 0 ? (int)g_cs.delay_ms : 8000;
                    log_line("switch: listen, then gentle page %d of %d", g_cs.attempt, HB_CS_TRIES);
                    write_status("disconnected waiting for %s", ini->name[0] ? ini->name : "-");
                    while (left > 0 && !hb_stop_requested() && !cmd_waiting()) {
                        int slice = left > 5000 ? 5000 : left;
                        if (listen_saved(hci, ini, &link, &av_psm, slice)) { r = 1; break; }
                        left -= slice;
                    }
                    if (r == 1) break;
                    hold_clear(ini->addr);
                    r = try_saved(hci, ini, &link, &av_psm, HB_PAGE_MS);
                }
                if (cs) note_event("%s", r == 1 ? "Headset reconnected" : "Could not reconnect. Press Connect");
                if (r != 1)
                    write_status("error %s %s", conn_fail_label(0), ini->name[0] ? ini->name : "-");
            } else if (g_npaired) {
                headset_ini first = g_paired[0];        /* most recent saved device */
                first.ok = first.have_addr = 1;
                hold_clear(first.addr);
                r = try_saved(hci, &first, &link, &av_psm, HB_PAGE_MS);
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
        g_cs.step = HB_CS_IDLE;
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
    set_target(ini->addr);                 /* others calling now: busy */

    prefs_attach(ini->addr);
    g_game_applied[0] = 0;                 /* a running game's profile over the headset's */
    game_tick();
    CTL_LOCK(&g_ctl);
    want_codec = g_ctl.codec_pref;
    CTL_UNLOCK(&g_ctl);
    {
        int av_ok;
        av.held_codec = g_prefs.held_codec;
        av.held_bp = g_prefs.held_bp;
        av_ok = avdtp_setup(&av, link, av_psm, want_codec, g_prefs.auto_no_xq);
        if (!av_ok && av.unsupported_format) {
            /* Retrying cannot help: the sink cannot take 48 kHz stereo. */
            g_kept_link = 0;
            log_line("stream: %s takes no 48 kHz stereo SBC; HearBridge has no resampler or "
                     "downmix, so it does not stream to it", ini->name[0] ? ini->name : "the headset");
            write_status("error unsupported-format %s", ini->name[0] ? ini->name : "-");
            notify("HearBridge: %s is not supported (needs 48 kHz stereo)",
                   ini->name[0] ? ini->name : "this device");
            goto done;
        }
        if (!av_ok && g_kept_link && !hb_stop_requested()) {
            /* Kept pairing link: AVDTP failed there — close, page, retry once. */
            log_line("stream: AVDTP on the pairing link failed — closing it and paging");
            btlink_disconnect(link);
            btlink_destroy(link);
            link = NULL;
            memset(&av, 0, sizeof av);
            av.held_codec = g_prefs.held_codec;
            av.held_bp = g_prefs.held_bp;
            {
                long w = now_ms() + 1000;
                while (now_ms() < w) idle_pump(hci, 50);
            }
            g_user_connect = 1;
            g_av_fail_ms = now_ms();
            av_ok = connect_and_probe(hci, ini, &link, &av_psm, HB_PAGE_MS) == 1 &&
                    avdtp_setup(&av, link, av_psm, want_codec, g_prefs.auto_no_xq);
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

stream_setup:
    if (want_codec != HB_CODEC_AUTO && av.codec.codec != want_codec) {
        /* The headset cannot take what was picked: plain SBC, and the page
         * and this headset's settings say so. */
        log_line("codec: %s does not fit this headset — using %s", hb_codec_key(want_codec),
                 hb_codec_key(av.codec.codec));
        want_codec = av.codec.codec;
        CTL_LOCK(&g_ctl);
        g_ctl.codec_pref = want_codec;
        g_ctl.prefs_dirty = 1;
        CTL_UNLOCK(&g_ctl);
    }
    memset(&scfg, 0, sizeof scfg);
    scfg.sample_rate = av.sink.sample_rate ? av.sink.sample_rate : 48000;
    scfg.channels = av.sink.channels ? av.sink.channels : 2;
    if (scfg.sample_rate != 48000 || scfg.channels != 2) {
        /* avdtp only configures 48 kHz stereo; never stream anything else. */
        log_line("stream: sink format %d Hz / %d ch is not 48 kHz stereo — not streaming",
                 scfg.sample_rate, scfg.channels);
        write_status("error unsupported-format %s", ini->name[0] ? ini->name : "-");
        goto done;
    }
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

    if (!cap) cap = avcap2_session_open();
    if (!cap) {
        log_line("stream: Avcap2 open failed");
        goto done;
    }

    memset(&pk, 0, sizeof pk);
    hb_tune_init(&pk.tune);
    pk.av = &av;
    pk.enc = enc;
    pk.link = link;
    pk.samples_per = sbc_encoder_frame_samples(enc);
    if (pk.samples_per <= 0) pk.samples_per = 128;
    mtu = btlink_chan_peer_mtu(link, av.media_scid);
    if (!mtu) mtu = 672;
    pk.mtu = (int)mtu;
    pk.rate_hz = scfg.sample_rate;
    hb_linkq_init(&lq);
    hb_lat_backoff_init(&bo);
    CTL_LOCK(&g_ctl);
    pk.queue_ms = hb_latency_clamp(g_ctl.latency_ms);
    g_ctl.lat_backoff_ms = 0;
    CTL_UNLOCK(&g_ctl);
    packer_size(&pk);
    {
        /* Media queue from the first packet (tune_link() keeps it in step). */
        int pkt_ms = pk.per_pkt * pk.samples_per * 1000 / pk.rate_hz;
        btlink_set_media_cap(link, hb_media_queue_cap(pkt_ms, pk.queue_ms));
        log_line("stream: latency target %d ms, media queue %d packets (~%d ms)",
                 pk.queue_ms, btlink_media_cap(link), btlink_media_cap(link) * pkt_ms);
    }
    hb_rate_init(&pk.rate, av.bitpool_lo ? av.bitpool_lo : av.bitpool,
                 av.bitpool_hi ? av.bitpool_hi : av.bitpool, sbc_encoder_bitpool(enc), now_ms());
    /* SBC HQ may climb above 53 (to the sink's maximum); SBC-XQ (dual
     * channel) stops at its own ceiling. */
    hb_rate_set_ceiling(&pk.rate, av.bitpool_hi ? av.bitpool_hi : av.bitpool, av.codec.ceil);
    log_line("stream: media MTU %u, SBC frame %d bytes, %d frames/packet, bitpool %d "
             "(adapts %d-%d)", mtu, pk.fsz, pk.per_pkt, sbc_encoder_bitpool(enc),
             pk.rate.lo, pk.rate.hi);

    /* Debug only: create /data/hearbridge/media_dump to write the first
     * media payloads to media_dump.bin (checked with tests/decode_dump). */
    if (file_exists(DUMP_FLAG_PATH)) {
        if (avdtp_dump_open(&av, DUMP_PATH, DUMP_PKTS))
            log_line("stream: dumping first %d media payloads to %s", DUMP_PKTS, DUMP_PATH);
        else
            log_line("stream: media dump %s not writable", DUMP_PATH);
    }
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

    hb_eq_init(&eq);
    hb_night_init(&night, scfg.sample_rate);
    hb_batt_alert_reset(&balert);
    CTL_LOCK(&g_ctl);
    hb_night_set(&night, g_ctl.night);
    eq_seq = g_ctl.eq_seq;
    hb_eq_set(&eq, g_ctl.eq_on, g_ctl.eq_db, scfg.sample_rate);
    gain_pct = g_ctl.gain_pct;
    muted = g_ctl.muted;
    tone = g_ctl.tone || tone_file;
    g_ctl.sample_rate = scfg.sample_rate;
    g_ctl.bitpool = av.bitpool;
    snprintf(g_ctl.codec, sizeof g_ctl.codec, "%s", av.codec.name ? av.codec.name : "SBC");
    g_ctl.codec_avail = av.codec.avail;
    CTL_UNLOCK(&g_ctl);
    log_line("stream: base gain %d%%%s%s", gain_pct, muted ? ", muted" : "",
             tone ? ", TEST TONE 1 kHz -6 dB" : "");

    write_status("connected %s", ini->name[0] ? ini->name : "-");
    ctl_set_state("streaming", ini->name);
    if (!switched) notify("hearbridge: connected %s", ini->name[0] ? ini->name : "headphones");
    else notify("HearBridge: now %s", av.codec.name ? av.codec.name : "SBC");
    gain_limiter_reset();
    log_line("stream: streaming until stop file or link drop");
    log_line("stream: playing on %s, %s %d kHz", ini->name[0] ? ini->name : "headset",
             av.codec.name ? av.codec.name : "SBC", pk.rate_hz / 1000);
    note_event("Playing on %s", ini->name[0] ? ini->name : "the headset");

    samples = 0;
    xq_bad_s = xq_low_s = 0;
    xq_drops = btlink_tx_dropped(link);
    t_start = t_stat = now_ms();
    memcpy(g_stream_addr, ini->addr, 6);
    g_switch_req = 0;
    g_stream_up = 1;
    g_rest_resume = g_rest_wake = g_rest_stop = 0;
    for (;;) {
        int nframes, req_vol = -1, req_disc = 0, changed = 0, req_codec = -1;
        float peak = 0.f;
        long now, ahead_ms;

        if (hb_stop_requested()) { rc = RUN_STOP; break; }
        if (btlink_pump(link, 1) < 0 || !btlink_is_up(link)) {
            /* 0x13 remote user: buds went in the case or were switched
             * off, a disconnect, not a reason to page. 0x08 supervision
             * timeout is the radio link lost (out of range, interference):
             * the normal rejoin runs, like any other drop. */
            int dr = btlink_last_disc_reason();
            if (hb_drop_is_away(dr)) {
                note_event("Headset turned off");
                set_why("away");
                rc = RUN_AWAY;
            } else if (dr == 0x08) {
                log_line("stream: link lost (supervision timeout), rejoining");
                note_event("Connection lost");
                set_why("dropped");
                rc = RUN_DROPPED;
            } else {
                note_event("Connection lost");
                set_why("dropped");
                rc = RUN_DROPPED;
            }
            break;
        }
        if (av.remote_closed) {
            note_event("Headset stopped the audio");
            set_why("closed");
            rc = RUN_DROPPED;
            break;
        }
        if (!btlink_chan_is_open(link, av.media_scid)) {
            note_event("Headset stopped the audio");
            set_why("closed");
            rc = RUN_DROPPED;
            break;
        }
        if (btlink_media_rebind_due(link, av.media_scid))
            (void)avdtp_rebind(&av);
        if (btlink_ms_since_credit(link) > 4000) {
            log_line("stream: no packet acknowledged for 4 s, link lost");
            note_event("Connection lost");
            set_why("quiet");
            rc = RUN_DROPPED;
            break;
        }

        /* Web requests + headset volume (cheap; under the lock). */
        {
            int v = btlink_avrcp_volume(link, &changed);
            avst = btlink_avrcp_state(link);
            CTL_LOCK(&g_ctl);
            if (g_ctl.req_reset) {
                g_ctl.req_reset = 0;
                CTL_UNLOCK(&g_ctl);
                note_event("Connection reset");
                btlink_disconnect(link);   /* our handle only */
                set_why("off");
                rc = RUN_AWAY;
                break;
            }
            if (!vol_applied && (avst & 1) && (avst & 6)) {
                /* Volume control is up: the volume this headset was left at
                 * by the user, else 50 %. A page move meanwhile wins. */
                vol_applied = 1;
                if (g_ctl.req_hs_volume < 0) {
                    g_ctl.req_hs_volume = hb_prefs_hs_volume(&g_prefs);
                    vol_auto = 1;
                }
            }
            req_vol = g_ctl.req_hs_volume;
            g_ctl.req_hs_volume = -1;
            req_disc = g_ctl.req_disconnect;
            g_ctl.req_disconnect = 0;
            /* Bit 0: channel open, or the headset has registered / reported
             * volume (avrcp_reported). Same test as the status chip, so a
             * live percentage cannot sit next to "not connected". */
            if (avst & 1) g_ctl.hs_volume = v;
            else g_ctl.hs_volume = -1;
            if (req_vol >= 0) g_ctl.hs_volume = req_vol;
            /* The headset applies its own volume (it took SetAbsoluteVolume or
             * answered our registration): software gain stays at the base,
             * else the level would be lowered twice. Otherwise the headset
             * volume scales the software gain. */
            hs_vol = (avst & 8) ? -1 : (avst & 2) || req_vol >= 0 ? g_ctl.hs_volume : -1;
            gain_pct = g_ctl.gain_pct;
            muted = g_ctl.muted;
            tone = g_ctl.tone || tone_file;
            {
                /* Buffer target in effect: the slider, stepped back while a
                 * low target makes the link drop (hb_lat_backoff). */
                int want_q = hb_lat_effective(&bo, g_ctl.latency_ms);
                if (want_q != pk.queue_ms) {
                    pk.queue_ms = want_q;
                    lat_changed = 1;
                }
            }
            if (g_ctl.codec_pref != want_codec) req_codec = g_ctl.codec_pref;
            if (g_ctl.eq_seq != eq_seq) {
                int on = g_ctl.eq_on, db[HB_EQ_NB];
                memcpy(db, g_ctl.eq_db, sizeof db);
                eq_seq = g_ctl.eq_seq;
                hb_eq_set(&eq, on, db, scfg.sample_rate);
            }
            if (g_ctl.night != night.on) {
                hb_night_set(&night, g_ctl.night);
                ctl_event_locked(&g_ctl, night.on ? "Night mode on" : "Night mode off");
            }
            g_ctl.night_db10 = hb_night_gain_db10(&night);
            if (g_rest_stop) {
                g_rest_stop = 0;
                rest_now = 1;
            }
            g_ctl.avrcp = avst;
            g_ctl.hs_moves = btlink_avrcp_headset_moves(link);
            CTL_UNLOCK(&g_ctl);
            if (changed) {
                log_line("stream: headset volume %d/127 -> gain", v);
                /* Moved on the headset itself: next time it starts there too
                 * (saved with the next status tick, not on every step). */
                if (g_prefs_have && !g_game_applied[0] && req_vol < 0 && g_prefs.hs_vol != v) {
                    g_prefs.hs_vol = v;
                    hs_dirty = 1;
                }
            }
            if (req_vol >= 0) {
                btlink_avrcp_set_volume(link, req_vol);
                if (vol_auto) {
                    log_line("stream: headset volume %d/127 (%s)", req_vol,
                             g_prefs.hs_vol >= 0 ? "set by you before" : "default 50%");
                    vol_auto = 0;
                } else if (g_prefs_have && !g_game_applied[0] && g_prefs.hs_vol != req_vol) {
                    g_prefs.hs_vol = req_vol;      /* moved on the page: kept for it */
                    prefs_save();
                }
            }
            gain_milli = ctl_effective_gain_milli(gain_pct, muted, hs_vol);
            if (lat_changed) {
                /* New latency target: packet size and queue follow now. */
                int pkt_ms;
                lat_changed = 0;
                packer_size(&pk);
                pkt_ms = pk.per_pkt * pk.samples_per * 1000 / pk.rate_hz;
                btlink_set_media_cap(link, hb_media_queue_cap(pkt_ms, pk.queue_ms));
                log_line("stream: latency target %d ms — %d frames/packet, media queue %d packets (~%d ms)",
                         pk.queue_ms, pk.per_pkt, btlink_media_cap(link), btlink_media_cap(link) * pkt_ms);
            }
        }
        if (req_codec >= 0) {
            log_line("stream: codec %s picked on the page", hb_codec_key(req_codec));
            cs_want = req_codec;
            cs_no_xq = g_prefs.auto_no_xq;
        }
        if (cs_want >= 0) {
            /* New codec = new AVDTP configuration. Done on the open link
             * (no disconnect: some headsets, the Xbox one included, do not
             * answer pages for minutes after we drop them). */
            int ok;
            persist_gain_if_dirty();
            ok = codec_switch_in_place(&av, link, cs_want, cs_no_xq);
            want_codec = cs_want;
            cs_want = -1;
            if (ok) {
                sbc_encoder_close(enc);
                enc = NULL;
                switched = 1;
                goto stream_setup;
            }
            note_event("Lost the headset while changing quality, reconnecting");
            memset(&g_pending, 0, sizeof g_pending);
            g_pending.kind = CMD_RECONNECT;
            rc = RUN_SWITCH;
            break;
        }
        if (g_switch_req) {
            int bi = paired_find(g_paired, g_npaired, g_switch_addr);
            long age = acl_track_request_age(g_switch_addr, now_ms());
            if (bi >= 0 && age >= 0 && age < ACL_REQ_PENDING_MS) {
                memcpy(switch_from, ini->addr, 6);
                note_event("Switching to %s, %s disconnected",
                           g_paired[bi].name[0] ? g_paired[bi].name : "the other headset",
                           ini->name[0] ? ini->name : "the headset");
                rc = RUN_SWITCH_IN;
                break;
            }
            g_switch_req = 0;                  /* gone meanwhile */
        }
        if (rest_now) {
            /* Rest mode on the way: stop the headset cleanly now (same
             * teardown as Disconnect), main brings it back after the wake. */
            note_event("Rest mode, headset paused until the console wakes");
            set_why("rest");
            g_rest_resume = 1;
            rc = RUN_PAUSED;
            break;
        }
        if (req_disc) {
            note_event("Disconnected");
            set_why("off");
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
            int op;
            hb_night_process(&night, pcm, nframes);      /* before EQ / gain / limiter */
            op = gain_apply_soft_eq(pcm, nframes * 2, gain_milli, &eq);
            if (op > out_peak) out_peak = op;
        }
        if (!packer_feed(&pk, pcm, nframes)) break;
        samples += nframes;

        if (now - t_stat >= 1000) {
            {
                int pkt_ms = pk.per_pkt * pk.samples_per * 1000 / pk.rate_hz;
                int q10 = pk.bl_n > 0 ? (int)(pk.bl_sum * 10 / pk.bl_n) : 0;
                hb_latency_estimate(&lat, pkt_ms, q10, (int)btlink_acl_gap_avg(link),
                                    av.delay_on ? av.sink_delay_x10 : 0);
                pk.bl_sum = pk.bl_n = 0;
            }
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
            g_ctl.lat_total = lat.total_ms;
            g_ctl.lat_capture = lat.capture_ms;
            g_ctl.lat_packet = lat.packet_ms;
            g_ctl.lat_queue = lat.queue_ms;
            g_ctl.lat_radio = lat.radio_ms;
            g_ctl.lat_sink = lat.sink_ms;
            g_ctl.lat_sink_reported = lat.sink_reported;
            {
                /* Headset extras for the page: battery, link meter. */
                int rssi, lqv, dpm = hb_linkq_drops_per_min(&lq, btlink_tx_dropped(link), now);
                btlink_link_quality(link, &rssi, &lqv);
                g_ctl.battery = btlink_avrcp_battery(link);
                g_ctl.batt_pct = btlink_hfp_battery(link);
                {
                    /* Volume fallback: while the page is open (it polls
                     * /api/status) and the headset has sent no volume report
                     * for 3 s, ask it again, at most every 3 s. Page closed:
                     * no extra traffic (DualSense-friendly). */
                    static const btlink *vq_link;
                    static unsigned long vq_polls, vq_reports;
                    static long vq_page_t, vq_rep_t, vq_last;
                    unsigned long cmds, rsps, rep;
                    int refused;
                    if (vq_link != link) {
                        vq_link = link; vq_polls = g_ctl.status_polls; vq_reports = 0;
                        vq_page_t = 0; vq_rep_t = now; vq_last = now;
                    }
                    if (g_ctl.status_polls != vq_polls) { vq_polls = g_ctl.status_polls; vq_page_t = now; }
                    btlink_avrcp_stats(link, &cmds, &rsps, &rep, &refused);
                    if (rep != vq_reports) { vq_reports = rep; vq_rep_t = now; }
                    if (vq_page_t && now - vq_page_t < HB_VOLQ_PAGE_MS && now - vq_rep_t >= HB_VOLQ_MS &&
                        now - vq_last >= HB_VOLQ_MS && btlink_avrcp_requery(link))
                        vq_last = now;
                    diag_set("avrcp", "rx commands %lu, responses %lu, volume reports %lu%s", cmds, rsps, rep,
                             refused ? ", headset refused volume registration" : "");
                }
                {
                    /* Nothing from HFP or AVRCP a few seconds into the stream:
                     * the page says the headset does not show it. */
                    static const btlink *batt_link;
                    static long batt_since;
                    if (batt_link != link) { batt_link = link; batt_since = now; }
                    g_ctl.batt_none = g_ctl.batt_pct < 0 && g_ctl.battery < 0 &&
                                      now - batt_since > HB_BATT_WAIT_MS;
                }
                {
                    int b = g_ctl.battery,
                        warn = hb_batt_alert_step(&balert, g_ctl.batt_pct >= 0 ? g_ctl.batt_pct : avrcp_battery_level(b),
                                                  b == AVRCP_BATT_EXTERNAL || b == AVRCP_BATT_FULL);
                    if (warn) {
                        char evl[HB_EVENT_LEN];
                        g_ctl.batt_alert = warn;
                        g_ctl.batt_alert_seq++;
                        snprintf(evl, sizeof evl, "Headset battery at %d%%", warn);
                        ctl_event_locked(&g_ctl, evl);
                    }
                }
                g_ctl.link_rssi = rssi;
                g_ctl.link_lq = lqv;
                g_ctl.drops_min = dpm;
                g_ctl.link_score = hb_linkq_score(rssi, lqv, dpm,
                                                  btlink_tx_backlog(link) > HB_RATE_SLACK ? btlink_tx_backlog(link) - HB_RATE_SLACK : 0,
                                                  btlink_media_cap(link));
                {
                    int was = bo.extra_ms;
                    (void)hb_lat_backoff_tick(&bo, g_ctl.latency_ms, dpm);
                    if (bo.extra_ms != was) {
                        char evl[HB_EVENT_LEN];
                        g_ctl.lat_backoff_ms = bo.extra_ms;
                        if (bo.extra_ms > was)
                            snprintf(evl, sizeof evl, "Weak signal, latency raised by %d ms for now", bo.extra_ms);
                        else if (bo.extra_ms)
                            snprintf(evl, sizeof evl, "Signal better, latency %d ms above your setting", bo.extra_ms);
                        else
                            snprintf(evl, sizeof evl, "Signal good, latency back to your setting");
                        ctl_event_locked(&g_ctl, evl);
                    }
                }
                if (pk.queue_ms < HB_QUEUE_LOW_MS) {
                    lat_sum = 0;
                    lat_n = 0;
                } else if (now - t_start > 8000) {
                    /* the delay at the default 200 ms buffer, to show what a
                     * lower target saves */
                    lat_sum += lat.total_ms;
                    if (++lat_n >= 5) {
                        g_ctl.lat_normal_ms = (int)(lat_sum / lat_n);
                        lat_sum = 0;
                        lat_n = 0;
                    }
                }
            }
            CTL_UNLOCK(&g_ctl);
            if (hs_dirty) {
                hs_dirty = 0;
                prefs_save();
            }
            bg_tick(ini);
            tune_link(&pk, now);
            if (hb_rate_settled(&pk.rate, now)) {
                int bp = sbc_encoder_bitpool(enc);
                if (bp > 0 && (bp != g_prefs.held_bp || av.codec.codec != g_prefs.held_codec)) {
                    if (++settle_s >= 15) {
                        g_prefs.held_codec = av.codec.codec;
                        g_prefs.held_bp = bp;
                        prefs_save();
                        log_line("prefs: this link holds %s at bitpool %d",
                                 hb_codec_key(av.codec.codec), bp);
                    }
                }
            } else {
                settle_s = 0;
            }
            if (want_codec == HB_CODEC_AUTO && av.codec.codec == HB_CODEC_SBC_XQ) {
                /* Auto picked SBC-XQ but the link cannot carry it: still
                 * dropping at the bottom of the range for 10 s. Remember that
                 * for this headset and reconnect with plain SBC. */
                long d = btlink_tx_dropped(link);
                xq_bad_s = (d > xq_drops && pk.rate.cur <= pk.rate.lo + 2) ? xq_bad_s + 1 : 0;
                xq_drops = d;
                /* Holding but far below its 38: dual channel at under 30 per
                 * channel sounds worse than joint stereo SBC at 51-53. */
                xq_low_s = pk.rate.cur < HB_XQ_LOW_BP ? xq_low_s + 1 : 0;
                if (xq_low_s >= 30) {
                    log_line("stream: SBC-XQ stuck at bitpool %d (under %d)", pk.rate.cur, HB_XQ_LOW_BP);
                    CTL_LOCK(&g_ctl);
                    g_ctl.xq_low = 1;
                    CTL_UNLOCK(&g_ctl);
                    xq_bad_s = 10;
                }
                if (xq_bad_s >= 10) {
                    note_event("Best quality was unstable, using standard quality for this headset");
                    g_prefs.auto_no_xq = 1;
                    prefs_save();
                    cs_want = HB_CODEC_AUTO;      /* switched in place on the next pass */
                    cs_no_xq = 1;
                    xq_bad_s = 0;
                }
            }
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
                    if (c.kind == CMD_FORGET_CUR) {
                        note_event("Disconnecting %s to forget it", ini->name[0] ? ini->name : "the headset");
                        memcpy(forget_addr, c.addr, 6);
                        rc = RUN_FORGOT;
                        break;
                    } else if (c.kind == CMD_SCAN && c.user) {
                        /* Scan / Add headset while streaming: no inquiry next to
                         * a live stream (the radio is shared with the DualSense,
                         * it could drop the pad). The page says the same. */
                        note_event("Disconnect %s first to add a new headset",
                                   ini->name[0] ? ini->name : "the headset");
                    } else if (c.kind == CMD_SCAN) {
                        /* A page load asks for a refresh scan. Don't drop a live headset for it. */
                        log_line("scan: refresh scan while the headset is up, not dropping it");
                    } else if (!same && c.kind != CMD_NONE) {
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
    if (rc == RUN_DROPPED || rc == RUN_AWAY) {
        write_status("disconnected");
        ctl_set_state("disconnected", ini->name);
    }

done:
    if (cap) avcap2_session_close(cap);
    if (enc) sbc_encoder_close(enc);
    if (av.link) avdtp_teardown(&av);
    if (link) {
        btlink_disconnect(link);
        if ((rc == RUN_SWITCH || rc == RUN_SWITCH_IN || rc == RUN_PAUSED) && ini->ok)
            hb_dropped_note(&g_dropped, ini->addr, now_ms());   /* its callback: accepted + closed cleanly */
        if (rc == RUN_SWITCH || rc == RUN_SWITCH_IN) {
            /* Only one headset at a time: wait for the old link to be gone. */
            log_line("switch: old headset %s", btlink_last_close_confirmed()
                     ? "disconnected (confirmed)" : "close not confirmed — waiting 1 s");
            if (!btlink_last_close_confirmed()) usleep(1000 * 1000);
        }
        btlink_destroy(link);
    }
    g_stream_up = 0;
    set_target(NULL);
    if (rc == RUN_DROPPED || rc == RUN_AWAY || rc == RUN_PAUSED || rc == RUN_SWITCH_IN) {
        /* Turned off / out of range / case, or Disconnect pressed: AVDTP,
         * AVRCP, L2CAP and our ACL handle went with the link above. Clear what outlives it, so the
         * next connection starts clean; the key and settings stay saved. */
        if (ini->ok) acl_track_request_clear(ini->addr);   /* a call from before the drop */
        g_av_fail_ms = 0;
        g_kept_link = 0;
        if (g_cs.step != HB_CS_IDLE) {
            log_line("stream: codec switch state cleared by the drop");
            g_cs.step = HB_CS_IDLE;
        }
        log_line("stream: link state cleared (headset stays saved)");
    }
    if (rc == RUN_FORGOT) {
        /* AVDTP closed, L2CAP closed, our ACL disconnected (0x13) and its
         * Disconnection Complete waited for above: now delete it. */
        log_line("saved: forgot a device (the current one) — after %s disconnect",
                 btlink_last_close_confirmed() == 1 ? "a confirmed" : "an unconfirmed");
        forget_device(forget_addr, ini, 1);
        note_event("Forgot the headset");
        memset(&g_pending, 0, sizeof g_pending);
        g_pending.kind = CMD_SCAN;         /* show the chooser, as before */
        write_status("scanning");
        ctl_set_state("scanning", NULL);
        CTL_LOCK(&g_ctl);
        g_ctl.device[0] = 0;
        CTL_UNLOCK(&g_ctl);
    }
    if (rc == RUN_SWITCH_IN) {
        /* A is fully down (same teardown as Disconnect) and held so it does
         * not call back over B. Now take B's waiting call. */
        int bi = paired_find(g_paired, g_npaired, g_switch_addr);
        hold_add(switch_from, 0);
        g_switch_req = 0;
        rc = RUN_SWITCH;
        if (bi >= 0) {
            headset_ini b = g_paired[bi];
            btlink *bl = NULL;
            unsigned bpsm = 0;
            b.ok = b.have_addr = 1;
            set_target(b.addr);
            if (listen_saved(hci, &b, &bl, &bpsm, 3000)) {
                *ini = b;
                keep_identity(ini);
                if (!headset_ini_save(ini)) log_line("saved: cannot write headset.ini");
                remember_device(ini);
                publish_saved(ini);
                g_ready = bl;
                g_ready_psm = bpsm;
                log_line("switch: %s is up", ini->name[0] ? ini->name : "the new headset");
            } else {
                log_line("switch: its call is gone — paging it");
                memset(&g_pending, 0, sizeof g_pending);
                g_pending.kind = CMD_ADDR;
                memcpy(g_pending.addr, b.addr, 6);
            }
            set_target(NULL);
        }
        if (!g_ready) {
            write_status("disconnected %s", ini->name[0] ? ini->name : "-");
            publish_saved(NULL);
            ctl_set_state("connecting", NULL);
        }
    } else if (rc == RUN_SWITCH) {
        write_status("disconnected %s", ini->name[0] ? ini->name : "-");
        publish_saved(NULL);
        ctl_set_state("connecting", NULL);
    }
    if (rc != RUN_STOP && hb_stop_requested()) rc = RUN_STOP;
    return rc;
}

/* Home-screen tile: rewritten and registered again on every run, so an
 * icon deleted from the home screen comes back by running the ELF again.
 * second = another instance is already running: only (re)register, never
 * process remove_tile (the running instance did that). */
static void home_tile(int second)
{
    if (!second && access(RM_TILE_PATH, F_OK) == 0) {
        tile_uninstall();
        (void)diag_save();
        unlink(RM_TILE_PATH);
        { FILE *f = fopen(NO_TILE_PATH, "w"); if (f) fclose(f); }
    } else if (access(NO_TILE_PATH, F_OK) != 0 && access(RM_TILE_PATH, F_OK) != 0) {
        char u[200] = "";
        tile_report tr;
        FILE *f = fopen(TILE_URL_PATH, "r");
        if (f) {
            if (!fgets(u, sizeof u, f)) u[0] = 0;
            fclose(f);
            u[strcspn(u, "\r\n ")] = 0;
            if (!strcmp(u, "start")) snprintf(u, sizeof u, "%s", HB_TILE_START_URL);
        }
        if (tile_install(u, &tr) != 0)
            notify("HearBridge: home-screen icon not added (%s, code %#x). Details: %s",
                   tr.failed ? tr.failed : "?", (unsigned)tr.code, second ? LOG_PATH : DIAG_PATH);
    } else {
        diag_set("tile", "disabled (%s exists)", NO_TILE_PATH);
    }
}

/* When Bluetooth cannot start, keep the web page (and
 * /api/diag) up until Stop is pressed or the stop file appears, instead of
 * exiting at once, so the report can be read from a phone or PC. */
static void bt_failed_wait(const char *status)
{
    write_status("%s", status);
    log_line("hearbridge: %s - page and /api/diag stay up until Stop", status);
    while (!hb_stop_requested()) usleep(500 * 1000);
}


/* 1 = g_ready is up. 2 = the page asked for something. 0 = stop. */
static int gentle_rejoin_(hci_t hci, headset_ini *ini);
static int gentle_rejoin(hci_t hci, headset_ini *ini)
{
    int r;
    memcpy(g_rejoin_addr, ini->addr, 6);
    g_rejoining = 1;
    r = gentle_rejoin_(hci, ini);
    g_rejoining = 0;
    return r;
}
static int gentle_rejoin_(hci_t hci, headset_ini *ini)
{
    int pages = 0;
    log_line("rejoin: waiting for the headset to connect in");
    note_event("Waiting for the headset to come back");
    write_status("disconnected waiting for %s", ini->name[0] ? ini->name : "-");
    ctl_set_state("disconnected", ini->name);
    (void)woke_up();
    for (;;) {
        int listen, left, sit, j;
        if (hb_stop_requested()) return 0;
        if (transport_dead(hci)) return 0;    /* main reopens Bluetooth */
        if (woke_up()) {
            note_event("Console woke up, looking for the headset");
            pages = 0;
        }
        bg_tick(ini);
        {
            int go = 0, reset = 0;
            CTL_LOCK(&g_ctl);
            reset = g_ctl.req_reset;
            CTL_UNLOCK(&g_ctl);
            if (reset) return 0;          /* idle path clears our page / ACL */
            {
                int off;
                CTL_LOCK(&g_ctl);
                off = g_ctl.paused || g_ctl.req_disconnect;
                g_ctl.req_disconnect = 0;
                if (off) g_ctl.paused = 1;
                CTL_UNLOCK(&g_ctl);
                if (off) {                /* Disconnect while it was coming back */
                    log_line("rejoin: stopped by Disconnect");
                    note_event("Disconnected");
                    set_why("off");
                    if (ini->ok) hold_add(ini->addr, 1);
                    return 0;
                }
            }
            CTL_LOCK(&g_ctl);
            go = g_ctl.req_connect;
            g_ctl.req_connect = 0;
            if (go) g_ctl.paused = 0;
            CTL_UNLOCK(&g_ctl);
            if (go && !g_pending.kind) {
                (void)poll_cmd(&g_pending, ini);
                if (!g_pending.kind) g_nhold = 0;
            }
            if (!go && poll_cmd(&g_pending, ini)) go = 1;
            if (go) {
                g_user_connect = 1;
                return 2;
            }
        }
        sit = !hb_re_paging(pages);
        listen = sit ? 8000 : hb_re_listen_ms(pages);
        for (left = listen; left > 0; ) {
            int slice = left > 5000 ? 5000 : left;
            /* Any saved headset, not only the one that dropped: one taken
             * out of its case meanwhile connects and plays right away,
             * without the page (build 20 log: it was turned down busy or
             * left unanswered until the page sent a command). */
            if (listen_any_saved(hci, ini, &g_ready, &g_ready_psm, slice)) {
                note_event("Headset reconnected");
                return 1;
            }
            (void)woke_up();                  /* our own wait is not a wake */
            if (hb_stop_requested() || cmd_waiting()) break;
            left -= slice;
            if (sit) break;
        }
        if (hb_stop_requested()) return 0;
        if (cmd_waiting()) continue;          /* a pick: checked at the top */
        if (sit) continue;
        pages++;
        log_line("rejoin: gentle page %d of %d", pages, HB_RE_PAGES);
        g_user_connect = 0;
        g_bg_page = 1;
        j = try_saved(hci, ini, &g_ready, &g_ready_psm, HB_PAGE_MS);
        g_bg_page = 0;
        (void)woke_up();                      /* the page blocked up to 5 s */
        if (j) {
            note_event("Headset reconnected");
            return 1;
        }
    }
}

/* Rest mode resets the Bluetooth USB device: close everything and open it
 * again (quick tries first, then every 5 s) until it is back or Stop. */
static int reopen_bt(hci_t *hci, a2dp_session **asess, a2dp_open_opts *opts, headset_ini *ini)
{
    int attempt = 0;
    note_event("Bluetooth adapter reset, starting it again");
    write_status("reconnecting");
    ctl_set_state("reconnecting", NULL);
    if (*asess) a2dp_close(*asess);
    *asess = NULL;
    if (hci->ops && hci->ops->close) hci->ops->close(hci->ctx);
    memset(hci, 0, sizeof *hci);
    while (!hb_stop_requested()) {
        int left = hb_reopen_delay_ms(attempt++);
        while (left > 0 && !hb_stop_requested()) { usleep(250 * 1000); left -= 250; }
        if (hb_stop_requested()) break;
        if (!hci_usb_open(hci)) {
            if (attempt == 1 || attempt % 12 == 0) log_line("reopen: controller not back yet (try %d)", attempt);
            continue;
        }
        if (ini->have_addr) memcpy(opts->prefer_addr, ini->addr, 6);
        *asess = a2dp_open(*hci, opts);
        if (*asess) {
            log_line("bt: back after %d tries", attempt);
        note_event("Bluetooth is back");
            (void)diag_save();
            return 1;
        }
        log_line("reopen: controller setup failed (try %d)", attempt);
        if (hci->ops && hci->ops->close) hci->ops->close(hci->ctx);
        memset(hci, 0, sizeof *hci);
    }
    return 0;
}

int main(void)
{
    hci_t hci;
    a2dp_session *asess = NULL;
    a2dp_open_opts opts;
    headset_ini ini;
    int rc = 2, mk_errno = 0, lock_rc, lock_errno = 0, log_ok;

    /* First sign of life, before any file, lock or library work: if this
     * toast shows but nothing else happens, the payload did start and the
     * log/diag.txt say where it stopped; if it does not show, the loader
     * never ran it. */
    notify("HearBridge %s: starting", HEARBRIDGE_VERSION);

    if (mkdir(STATE_DIR, 0755) != 0 && errno != EEXIST) mk_errno = errno;
    unlink(DEVICES_JSON);      /* a list from an older run or build is stale */

    /* Log first so a lock problem is written down too (a second instance
     * only appends a few lines before it exits). */
    log_ok = log_open(LOG_PATH);
    diag_init(NULL);           /* file path set once we own the lock */
    diag_set("hearbridge", "%s (one build for all firmwares, compiled %s)", HEARBRIDGE_VERSION,
             __DATE__);
    /* A newly sent ELF always wins, whatever version is running (same
     * one included): the old instance is asked to stop (stop file +
     * SIGTERM; it closes the headset, the page and the Bluetooth device),
     * killed if it has not stopped after 10 s, and this one starts. */
    {
        long old_pid = 0;
        lock_rc = lock_take_over(LOCK_PATH, HB_STOP_PATH, 10000, 3000, &lock_errno, &old_pid);
        if (old_pid && lock_rc != LOCK_BUSY) {
            log_line("HearBridge PS5 %s: replaced the running instance (pid %ld)", HEARBRIDGE_VERSION, old_pid);
            notify("HearBridge %s: replaced the running copy", HEARBRIDGE_VERSION);
            usleep(500 * 1000);   /* let the controller settle after its teardown */
        }
    }
    if (lock_rc == LOCK_BUSY) {
        /* Could not stop it: still bring back a deleted icon. */
        log_line("HearBridge PS5 %s: the running instance would not stop; refreshing the home-screen icon only",
                 HEARBRIDGE_VERSION);
        home_tile(1);
        notify("HearBridge: already running and could not be stopped. Use Stop on the page, then run it again.");
        log_close();
        return 1;
    }
    diag_set_path(DIAG_PATH);
    if (lock_rc == LOCK_NO_WRITE) {
        /* Not "already running": the state folder is not writable. Carry on
         * so the page and /api/diag still work. */
        notify("HearBridge: cannot write %s (errno %d). Continuing; settings and logs may not be saved.",
               STATE_DIR, lock_errno);
    }

    hb_stop_init();
    log_line("HearBridge PS5 %s", HEARBRIDGE_VERSION);
    diag_set("state dir", "%s: mkdir errno %d; lock %s (errno %d); log %s", STATE_DIR, mk_errno,
             lock_rc == LOCK_OK ? "ok" : "NOT WRITABLE", lock_errno, log_ok ? "ok" : "NOT WRITABLE");
    (void)diag_save();
    log_line("attach to running controller (no reset); stop file %s", HB_STOP_PATH);
    if (file_exists(HCI_DEBUG_PATH)) {
        hcidbg_enable();
        log_line("debug: %s present: HCI trace at /api/hcilog, raw commands at /api/hci", HCI_DEBUG_PATH);
    }
    {
        char ct[32] = "";
        FILE *cf = fopen(CHIP_PATH, "r");
        if (cf) {
            if (!fgets(ct, (int)sizeof ct, cf)) ct[0] = 0;
            fclose(cf);
            btchip_set_override(btchip_parse_override(ct));
            log_line("chip: %s says \"%.20s\" -> %s", CHIP_PATH, ct,
                     btchip_get_override() >= 0 ? btchip_profile_name(btchip_get_override()) : "ignored (use mediatek or marvell)");
        }
    }
    ctl_init(&g_ctl, HEARBRIDGE_VERSION);
    snprintf(g_ctl.devices_path, sizeof g_ctl.devices_path, "%s", DEVICES_JSON);
    snprintf(g_ctl.select_path, sizeof g_ctl.select_path, "%s", SELECT_TXT);
    snprintf(g_ctl.saved_path, sizeof g_ctl.saved_path, "%s", SAVED_JSON);
    g_ctl.gain_pct = read_gain_pct();
    log_line("volume: base gain %d%% (%s)", g_ctl.gain_pct, GAIN_PATH);
    /* The old global mode file is the default for headsets without
     * their own setting yet. */
    g_ctl.latency_ms = HB_LAT_DEFAULT_MS;
    log_line("latency: default target %d ms for a new headset", g_ctl.latency_ms);
    {
        char url[64];
        int port;
        a2dp_inquiry_abort = cmd_waiting;
        a2dp_inquiry_progress = inquiry_progress;
        btlink_abort_connect = btlink_forget_abort;
        btlink_press_is_for = press_is_for;
        btlink_saved_peer = saved_peer;
        hci_usb_conn_req_hook = conn_req_hook;
        btlink_on_acl_up = on_acl_up;
        port = http_start(&g_ctl, url, (int)sizeof url);
        if (port) {
            CTL_LOCK(&g_ctl);
            snprintf(g_ctl.url, sizeof g_ctl.url, "%s", url);
            CTL_UNLOCK(&g_ctl);
            log_line("http: control page at %s", url);
            diag_set("web page", "%s (diagnostics at %s/api/diag)", url, url);
            notify("HearBridge %s: %s", HEARBRIDGE_VERSION, url);
        } else {
            log_line("http: control page could not start");
            diag_set("web page", "FAILED to start (ports %d-%d)", HB_HTTP_PORT, HB_HTTP_PORT + 5);
            notify("hearbridge: started\n%s", HEARBRIDGE_VERSION);
        }
    }

    /* Home-screen tile that opens the control page in the browser. */
    home_tile(0);

    games_load();
    CTL_LOCK(&g_ctl);
    games_publish_locked();
    CTL_UNLOCK(&g_ctl);

    /* Diagnostics: firmware, audio libraries and every USB device
     * (read-only). They run only after the page and the icon are up, so
     * everything before this point is the 1.0.2 startup path. */
    sysinfo_collect();
    (void)avcap2_probe();
    (void)hci_usb_survey();
    /* Game detection and the rest mode watch look their system calls up
     * here, one after the other: the kernel_dynlib_* helpers are not safe
     * to run from several threads at once (fw 10.20 lost lookups when the
     * game thread raced avcap2_probe). The threads start after that. */
    (void)hb_game_sys_avail();
    (void)hb_rest_sys_avail();
    g_game_thr_up = pthread_create(&g_game_thr, NULL, game_thread, NULL) == 0;
    g_rest_thr_up = pthread_create(&g_rest_thr, NULL, rest_thread, NULL) == 0;
    (void)diag_save();

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
        (void)diag_save();
        notify("HearBridge: HCI open failed");
        bt_failed_wait("error hci-open-failed");
        goto out;
    }
    {
        int vid, pid;
        if (hci_usb_chip(&vid, &pid)) {
            CTL_LOCK(&g_ctl);
            g_ctl.chip_vid = vid;
            g_ctl.chip_pid = pid;
            CTL_UNLOCK(&g_ctl);
        }
        (void)diag_save();
    }

    memset(&opts, 0, sizeof opts);
    opts.inquiry_seconds = 3;
    if (ini.have_addr) memcpy(opts.prefer_addr, ini.addr, 6);
    asess = a2dp_open(hci, &opts);
    if (!asess) {
        log_line("hearbridge: controller setup failed");
        diag_set("bt setup", "FAILED (a2dp_open)");
        (void)diag_save();
        notify("HearBridge: controller setup failed");
        bt_failed_wait("error controller-setup-failed");
        goto close_hci;
    }

    /* Stream until stopped; reconnect after a drop or failure. The web
     * page can pause (Disconnect) and resume (Connect). */
    for (;;) {
        int r, paused;
        if (transport_dead(hci)) {
            CTL_LOCK(&g_ctl);
            paused = g_ctl.paused;
            CTL_UNLOCK(&g_ctl);
            if (!reopen_bt(&hci, &asess, &opts, &ini)) { rc = 0; break; }
            (void)headset_ini_load(&ini);
            g_rest_resume = g_rest_wake = 0;   /* the reconnect below covers it */
            if (ini.ok && !paused && !held(ini.addr)) {
                /* Back from rest mode: the headset we were using, right away. */
                memset(&g_pending, 0, sizeof g_pending);
                g_pending.kind = CMD_RECONNECT;
            }
        }
        r = run_session(asess, hci, &ini);
        persist_gain_if_dirty();
        if (r == RUN_STOP) { rc = 0; break; }
        if (r == RUN_SWITCH || r == RUN_FORGOT) continue;   /* g_pending: the next step */
        CTL_LOCK(&g_ctl);
        paused = g_ctl.paused;
        g_ctl.req_connect = 0;
        CTL_UNLOCK(&g_ctl);
        /* A Disconnect on the page stays idle. A drop does not page hard:
         * listen, a few short pages, then sit until the headset connects in. */
        if ((r == RUN_PAUSED || paused) && ini.ok && !g_rest_resume) hold_add(ini.addr, 1);
        if (r == RUN_DROPPED) {
            g_want_until = 0;
            notify("HearBridge: connection lost");
        }
        /* Case / power-off (RUN_AWAY): stay disconnected and listen.
         * Don't page a headset that just went away. */
        if (r == RUN_DROPPED && !paused && ini.ok && !held(ini.addr)) {
            int j = gentle_rejoin(hci, &ini);
            if (j == 1) continue;
            if (hb_stop_requested()) { rc = 0; break; }
            if (j == 2) {
                (void)headset_ini_load(&ini);
                continue;
            }
        }
        publish_saved(NULL);
        if (r == RUN_PAUSED || paused || r == RUN_DROPPED || r == RUN_AWAY || !g_ctl.detail[0] ||
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
        if (g_npaired || ini.ok) btlink_page_scan_hold(hci, 1);   /* once for the idle period */
        {
            /* Auto-connect: headsets that page us on power-on are accepted
             * (listen below). For ones that only wait to be paged: one short
             * page of the most recent saved headset every 12 s, for 10 min,
             * never after a manual Disconnect or a case close (it pages us
             * itself when it comes out), never while a page command waits. */
            long idle_t0 = now_ms(), next_auto = now_ms() + 12000, window = 600000;
            int auto_ok = r != RUN_AWAY, auto_pages = 0;
            (void)woke_up();
            for (;;) {
                int go;
                if (hb_stop_requested()) break;
                if (transport_dead(hci)) break;          /* reopened at the top */
                if (g_rest_resume && (g_rest_wake || woke_up())) {
                    /* We stopped it for rest mode: the same headset, now. */
                    g_rest_resume = g_rest_wake = 0;
                    note_event("Console woke up, reconnecting the headset");
                    if (ini.ok) {
                        memset(&g_pending, 0, sizeof g_pending);
                        g_pending.kind = CMD_RECONNECT;
                        g_user_connect = 1;
                        break;
                    }
                }
                if (woke_up()) {
                    /* Rest mode ended: a fresh window of background pages,
                     * even after a case close (the headset may be on now). */
                    note_event("Console woke up, looking for the headset");
                    idle_t0 = now_ms();
                    window = HB_RESUME_PAGE_WINDOW_MS;
                    next_auto = now_ms() + 1500;
                    auto_ok = 1;
                    auto_pages = 0;
                }
                bg_tick(&ini);
                CTL_LOCK(&g_ctl);
                go = g_ctl.req_connect;
                g_ctl.req_connect = 0;
                g_ctl.req_disconnect = 0;     /* nothing up to disconnect: never carried into the next stream */
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
                {
                    int reset = 0;
                    CTL_LOCK(&g_ctl);
                    reset = g_ctl.req_reset;
                    if (reset) g_ctl.req_reset = 0;
                    CTL_UNLOCK(&g_ctl);
                    if (reset && ini.ok) {
                        unsigned h = acl_track_handle(ini.addr);
                        btlink *rl = btlink_create(hci, 1021, 7);
                        if (hci.ops && hci.ops->cmd)
                            hci.ops->cmd(hci.ctx, 0x0402, NULL, 0); /* our inquiry */
                        if (rl) {
                            if (hci.ops && hci.ops->cmd)
                                hci.ops->cmd(hci.ctx, 0x0408, ini.addr, 6); /* our page */
                            if (h) btlink_drop_handle(rl, h, 800); /* this headset only */
                            btlink_destroy(rl);
                        }
                        note_event("Connection reset");
                        log_line("link: reset our page and our headset ACL only");
                    }
                }
                persist_gain_if_dirty();
                if (g_npaired || ini.ok) {
                    btlink *back = NULL;
                    unsigned bpsm = 0;
                    if (answer_saved_calls(hci, &ini, &back, &bpsm) ||
                        listen_any_saved(hci, &ini, &back, &bpsm, 400)) {
                        g_ready = back;
                        g_ready_psm = bpsm;
                        break;
                    }
                    (void)woke_up();

                } else {
                    idle_pump(hci, 100);
                }
                CTL_LOCK(&g_ctl);
                paused = g_ctl.paused;
                CTL_UNLOCK(&g_ctl);
                if (auto_ok && !paused && ini.ok && !held(ini.addr) && !cmd_waiting() &&
                    hb_auto_page_ok(auto_pages) &&
                    now_ms() >= next_auto && now_ms() - idle_t0 < window) {
                    btlink *back = NULL;
                    unsigned bpsm = 0;
                    int got;
                    log_line("auto: one background page of %s", ini.name[0] ? ini.name : "the saved headset");
                    g_user_connect = 0;
                    btlink_page_scan_hold(hci, 0);
                    g_bg_page = 1;
                    got = try_saved(hci, &ini, &back, &bpsm, HB_PAGE_MS);
                    g_bg_page = 0;
                    (void)woke_up();              /* the page blocked up to 5 s */
                    auto_pages++;
                    if (!got && !hb_auto_page_ok(auto_pages))
                        log_line("auto: %d background pages done, now only listening", auto_pages);
                    if (got) {
                        note_event("%s turned on, connecting", ini.name[0] ? ini.name : "Headset");
                        g_ready = back;
                        g_ready_psm = bpsm;
                        break;
                    }
                    btlink_page_scan_hold(hci, 1);
                    set_why("");
                    write_status("disconnected");
                    ctl_set_state("disconnected", NULL);
                    next_auto = now_ms() + 12000;   /* 12 s after this attempt ended */
                }
            }
        }
        if (!transport_dead(hci)) btlink_page_scan_hold(hci, 0);   /* put back once */
        if (hb_stop_requested()) { rc = 0; break; }
        (void)headset_ini_load(&ini);
        continue;
    }

    if (asess) a2dp_close(asess);
close_hci:
    if (hci.ops && hci.ops->close) hci.ops->close(hci.ctx);
out:
    if (g_game_thr_up) {
        g_game_quit = 1;
        pthread_join(g_game_thr, NULL);
    }
    if (g_rest_thr_up) {
        g_game_quit = 1;
        pthread_join(g_rest_thr, NULL);
    }
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
