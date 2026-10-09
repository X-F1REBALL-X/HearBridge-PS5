#include "a2dp.h"
#include "devclass.h"
#include "../utf8.h"
#include "hci_cmd.h"
#include "log.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct a2dp_session {
    hci_t hci;
    unsigned char bdaddr[6];
    int have_addr;
    int acl_mtu;
    int acl_buffers;
    a2dp_open_opts opts;
};

/* Audio/Video major (4), or Audio service class bit (CoD bit 21). */
static int cod_audio_likely(uint32_t cod)
{
    unsigned major = (cod >> 8) & 0x1F;
    unsigned services = (cod >> 13) & 0x7FF;

    if (major == 4) return 1;           /* Audio/Video */
    if (services & (1u << 8)) return 1; /* Audio service (bit 21 of CoD) */
    return 0;
}

static const char *cod_minor_av(unsigned minor)
{
    switch (minor) {
    case 0x01: return "headset";
    case 0x02: return "hands-free";
    case 0x04: return "microphone";
    case 0x05: return "loudspeaker";
    case 0x06: return "headphones";
    case 0x07: return "portable-audio";
    case 0x08: return "car-audio";
    case 0x0A: return "hifi-audio";
    default:   return "av-other";
    }
}

static void log_dev(const a2dp_inq_dev *d)
{
    char astr[18];
    unsigned major = (d->cod >> 8) & 0x1F;
    unsigned minor = (d->cod >> 2) & 0x3F;
    const char *kind = major == 4 ? cod_minor_av(minor) : "non-av";

    hci_addr_str(d->addr, astr);
    if (d->have_rssi) {
        log_line("inquiry: %s CoD %06x (%s%s) RSSI %d name \"%s\"",
                 astr, (unsigned)d->cod, kind,
                 d->audio_likely ? ", audio?" : "",
                 d->rssi, d->name[0] ? d->name : "-");
    } else {
        log_line("inquiry: %s CoD %06x (%s%s) name \"%s\"",
                 astr, (unsigned)d->cod, kind,
                 d->audio_likely ? ", audio?" : "",
                 d->name[0] ? d->name : "-");
    }
}

/* Parse EIR for Complete (0x09) or Shortened (0x08) Local Name. */
static void eir_name(const unsigned char *eir, int eir_len, char *name, int name_max)
{
    int i = 0;

    if (!name || name_max < 2) return;
    name[0] = 0;
    while (i + 1 < eir_len) {
        int len = eir[i];
        int type;

        if (len == 0) break;
        if (i + 1 + len > eir_len) break;
        type = eir[i + 1];
        if ((type == 0x08 || type == 0x09) && len >= 2) {
            hb_utf8_copy(name, (size_t)name_max, (const char *)eir + i + 2, (size_t)(len - 1));
            return;
        }
        i += 1 + len;
    }
}

static int same_addr(const unsigned char a[6], const unsigned char b[6])
{
    return memcmp(a, b, 6) == 0;
}

static a2dp_inq_dev *find_or_add(a2dp_inq_dev *out, int max, int *n,
                                 const unsigned char addr[6])
{
    int i;

    for (i = 0; i < *n; i++) {
        if (same_addr(out[i].addr, addr)) return &out[i];
    }
    if (*n >= max) return NULL;
    memset(&out[*n], 0, sizeof out[*n]);
    memcpy(out[*n].addr, addr, 6);
    return &out[(*n)++];
}

/* rec layout: addr6, psrm1, reserved1, class3,
 * clock2, rssi1 (rssi may be 0 for plain Inquiry Result). */
static void ingest_rec(a2dp_inq_dev *out, int max, int *n,
                       const unsigned char *rec, int have_rssi,
                       const unsigned char *eir, int eir_len)
{
    a2dp_inq_dev *d;
    uint32_t cod;

    d = find_or_add(out, max, n, rec);
    if (!d) return;
    d->psrm = rec[6];
    /* Always mark clock valid when from inquiry (bit 15 of Clock_Offset, Core Vol 4 Part E 7.1.5). */
    d->clock_offset = (((unsigned)rec[11] | ((unsigned)rec[12] << 8)) & 0x7FFF) | 0x8000;
    cod = (uint32_t)rec[8] | (uint32_t)rec[9] << 8 | (uint32_t)rec[10] << 16;
    d->cod = cod;
    d->audio_likely = cod_audio_likely(cod);
    if (have_rssi) {
        d->rssi = (int8_t)rec[13];
        d->have_rssi = 1;
    }
    if (eir && eir_len > 0 && !d->name[0])
        eir_name(eir, eir_len, d->name, A2DP_NAME_MAX);
}

static void drain_acl(hci_t hci)
{
    unsigned char junk[HCI_PKT_MAX];

    if (!hci.ops || !hci.ops->next_acl) return;
    while (hci.ops->next_acl(hci.ctx, junk, (int)sizeof junk) > 0) { }
}

/* Best-effort Remote Name Request after inquiry. */
int (*a2dp_inquiry_abort)(void);
void (*a2dp_inquiry_progress)(const a2dp_inq_dev *devs, int n);
#define INQ_ABORTED() (a2dp_inquiry_abort && a2dp_inquiry_abort())

long a2dp_scan_deadline_ms;

#define PAST_DEADLINE() (a2dp_scan_deadline_ms && now_ms() >= a2dp_scan_deadline_ms)

/* Cancel a name request at the controller and wait for its completion,
 * so nothing is left outstanding before the next inquiry or page. */
static void name_req_cancel(hci_t hci, const unsigned char addr[6])
{
    unsigned char ev[HCI_PKT_MAX];
    long w;
    int done = 0;
    (void)hci_cmd_sync(hci, 0x041A, addr, 6, NULL, NULL, 0);
    w = now_ms() + 1000;
    while (now_ms() < w && !done) {
        int nEv;
        if (hci.ops->pump(hci.ctx, 30) < 0) return;
        while ((nEv = hci.ops->next_event(hci.ctx, ev, (int)sizeof ev)) > 0)
            if (ev[0] == 0x07 && nEv >= 9 && same_addr(ev + 3, addr)) { done = 1; break; }
        drain_acl(hci);
    }
    log_line("inquiry: name request cancelled%s", done ? "" : " (no completion seen)");
}

static void fetch_names(a2dp_session *s, a2dp_inq_dev *devs, int n)
{
    int order[A2DP_INQ_MAX];
    int i, j, left;
    hci_t hci = s->hci;

    if (n > A2DP_INQ_MAX) n = A2DP_INQ_MAX;
    for (i = 0; i < n; i++) order[i] = i;
    /* Audio devices first. */
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (devs[order[j]].audio_likely && !devs[order[i]].audio_likely) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }

    left = n < 6 ? n : 6; /* cap time on console */
    for (i = 0; i < left && !INQ_ABORTED() && !PAST_DEADLINE(); i++) {
        a2dp_inq_dev *d = &devs[order[i]];
        unsigned char p[10];
        long deadline;
        unsigned char ev[HCI_PKT_MAX];
        int got = 0;

        if (d->name[0]) continue;
        /* Only audio devices that would be listed (no TVs) need a name. */
        if (!d->audio_likely || hb_dev_rank_class(d->cod) < 0) continue;

        memcpy(p, d->addr, 6);
        p[6] = 0x01; /* R1 page scan repetition (common default) */
        p[7] = 0x00;
        p[8] = 0x00;
        p[9] = 0x00; /* clock offset unknown */
        if (!hci_cmd_status(hci, HB_OP_REMOTE_NAME_REQ, p, 10)) {
            log_line("inquiry: Remote Name Request failed to start");
            continue;
        }

        deadline = now_ms() + 2000;
        if (a2dp_scan_deadline_ms && deadline > a2dp_scan_deadline_ms)
            deadline = a2dp_scan_deadline_ms;
        while (now_ms() < deadline && !got) {
            int nEv, pr;

            if (INQ_ABORTED()) {
                /* A connect/reconnect is waiting. Drop this name lookup so the page can start. */
                (void)hci.ops->cmd(hci.ctx, 0x041A, d->addr, 6);
                log_line("inquiry: name request cancelled for a page command");
                return;
            }
            pr = hci.ops->pump(hci.ctx, 50);
            if (pr < 0) return;
            while ((nEv = hci.ops->next_event(hci.ctx, ev, (int)sizeof ev)) > 0) {
                /* Remote Name Request Complete 0x07: status, addr6, name */
                if (ev[0] == 0x07 && nEv >= 9 && same_addr(ev + 3, d->addr)) {
                    if (ev[2] == 0 && nEv > 9) {
                        hb_utf8_copy(d->name, A2DP_NAME_MAX, (const char *)ev + 9,
                                     (size_t)(nEv - 9));
                        if (a2dp_inquiry_progress) a2dp_inquiry_progress(devs, n);
                    }
                    got = 1;
                    break;
                }
            }
            drain_acl(hci);
        }
        if (!got) {
            log_line("inquiry: no name for device (timeout)");
            name_req_cancel(hci, d->addr);
        }
    }
}

a2dp_session *a2dp_open(hci_t hci, const a2dp_open_opts *opts)
{
    a2dp_session *s;
    unsigned char cc[64];
    int cc_len = 0;
    static const unsigned char one[1] = { 1 };

    s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->hci = hci;
    if (opts) s->opts = *opts;
    if (s->opts.inquiry_seconds <= 0) s->opts.inquiry_seconds = 12;

    /* Attach to the running controller: no HCI_Reset, no event mask, scan
     * mode, class or local name change. Only non-destructive writes: a widened
     * event mask and SSP on if it is off — neither drops links. */
    if (!hci_cmd_sync(hci, HB_OP_READ_BUFFER_SIZE, NULL, 0, cc, &cc_len, (int)sizeof cc)
        || cc_len < 13) {
        log_line("a2dp: READ_BUFFER_SIZE failed");
        free(s);
        return NULL;
    }
    s->acl_mtu = (int)((unsigned)cc[6] | ((unsigned)cc[7] << 8));
    s->acl_buffers = (int)((unsigned)cc[9] | ((unsigned)cc[10] << 8));
    log_line("a2dp: controller %d ACL buffers of %u bytes",
             s->acl_buffers, (unsigned)s->acl_mtu);

    /* Event mask: a superset of the spec default (0x00001FFFFFFFFFFF) plus
     * the SSP/auth events (IO Cap Request/Response, User Confirmation,
     * Simple Pairing Complete, Link Key Request/Notification, Encryption
     * Change ...), Inquiry Result with RSSI and Extended Inquiry Result
     * (bit 62). Only ever widens the default, so nothing the system stack
     * relies on is turned off. Page 2 is left untouched (no SSP events
     * live there). Value 0x7DBFFFFFFFFFFFFF, sent little-endian. */
    {
        static const unsigned char mask[8] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF, 0x7D
        };
        if (!hci_cmd_sync(hci, HB_OP_SET_EVENT_MASK, mask, 8, cc, &cc_len, (int)sizeof cc))
            log_line("a2dp: SET_EVENT_MASK not accepted (pairing events may be missing)");
        else
            log_line("a2dp: event mask 0x7DBFFFFFFFFFFFFF (SSP + EIR enabled)");
    }

    if (!hci_cmd_sync(hci, HB_OP_READ_SSP_MODE, NULL, 0, cc, &cc_len, (int)sizeof cc)
        || cc_len < 7 || cc[6] != 1) {
        if (!hci_cmd_sync(hci, HB_OP_WRITE_SSP_MODE, one, 1, cc, &cc_len, (int)sizeof cc)) {
            log_line("a2dp: WRITE_SSP_MODE failed");
            free(s);
            return NULL;
        }
        log_line("a2dp: Simple Pairing turned on");
    }

    if (hci_cmd_sync(hci, HB_OP_READ_BD_ADDR, NULL, 0, cc, &cc_len, (int)sizeof cc)
        && cc_len >= 12) {
        memcpy(s->bdaddr, cc + 6, 6);
        s->have_addr = 1;
        {
            char astr[18];
            hci_addr_str(s->bdaddr, astr);
            log_line("a2dp: controller address %s", astr);
        }
    } else {
        log_line("a2dp: READ_BD_ADDR failed (controller may still work)");
    }

    log_line("a2dp: session open (pair/AVDTP/SBC next)");
    return s;
}

void a2dp_close(a2dp_session *s)
{
    if (!s) return;
    free(s);
}

int a2dp_controller_ok(const a2dp_session *s)
{
    return s && s->have_addr;
}

int a2dp_local_addr(const a2dp_session *s, unsigned char out[6])
{
    if (!s || !s->have_addr || !out) return 0;
    memcpy(out, s->bdaddr, 6);
    return 1;
}

/* Inquiry Result (0x02) / with RSSI (0x22) carry parameter ARRAYS
 * (Core Vol 4 Part E 7.7.2 / 7.7.33): all addrs, then all PSRMs, ...
 * Converted to our 14-byte record: addr6 psrm res cod3 clk2 rssi. */
static void ingest_result_event(a2dp_inq_dev *out, int max, int *n,
                                const unsigned char *ev, int nEv)
{
    int num, i, with_rssi = ev[0] == 0x22;
    const int per = 14; /* 0x02: 6+1+2+3+2, 0x22: 6+1+1+3+2+1 bytes */
    const unsigned char *p = ev + 3;

    if (nEv < 3) return;
    num = ev[2];
    if (num <= 0 || 3 + num * per > nEv) return;
    for (i = 0; i < num; i++) {
        unsigned char rec[14];
        const unsigned char *q;
        memcpy(rec, p + i * 6, 6);                       /* BD_ADDR */
        rec[6] = p[num * 6 + i];                         /* PSRM */
        rec[7] = 0;
        if (with_rssi) {
            q = p + num * 8 + i * 3;                     /* after addr,psrm,res1 */
            memcpy(rec + 8, q, 3);
            q = p + num * 11 + i * 2;
            rec[11] = q[0]; rec[12] = q[1];
            rec[13] = p[num * 13 + i];
        } else {
            q = p + num * 9 + i * 3;                     /* after addr,psrm,res2 */
            memcpy(rec + 8, q, 3);
            q = p + num * 12 + i * 2;
            rec[11] = q[0]; rec[12] = q[1];
            rec[13] = 0;
        }
        ingest_rec(out, max, n, rec, with_rssi, NULL, 0);
    }
}

/* Handle one event seen while inquiring. Returns 1 on Inquiry Complete. */
static int inquiry_event(a2dp_inq_dev *out, int max, int *n,
                         const unsigned char *ev, int nEv)
{
    if (ev[0] == 0x01) {
        log_line("inquiry: complete (status %#04x)", nEv > 2 ? ev[2] : 0xFF);
        return 1;
    }
    if (ev[0] == 0x02 || ev[0] == 0x22)
        ingest_result_event(out, max, n, ev, nEv);
    else if (ev[0] == 0x2F && nEv >= 17 && ev[2] >= 1)
        /* Extended Inquiry Result: one record + 240-byte EIR. */
        ingest_rec(out, max, n, ev + 3, 1, ev + 17, nEv - 17);
    return 0;
}

/* Pump for up to ms, collecting results. Returns 1 on Inquiry Complete,
 * 0 on timeout, -1 on transport loss / stop. */
static int g_inq_events;   /* events seen during the current inquiry */

static int inquiry_listen(hci_t hci, a2dp_inq_dev *out, int max, int *n,
                          int ms, int *status_out, unsigned want_op)
{
    unsigned char ev[HCI_PKT_MAX];
    long until = now_ms() + ms;
    while (now_ms() < until) {
        int nEv;
        if (INQ_ABORTED()) return 3;
        if (hci.ops->pump(hci.ctx, 50) < 0) return -1;
        while ((nEv = hci.ops->next_event(hci.ctx, ev, (int)sizeof ev)) > 0) {
            g_inq_events++;
            if (want_op && status_out && ev[0] == 0x0F && nEv >= 6 &&
                ((unsigned)ev[4] | ((unsigned)ev[5] << 8)) == want_op) {
                *status_out = ev[2];
                return 2;
            }
            {
                int before = *n, done = inquiry_event(out, max, n, ev, nEv);
                if (*n != before && a2dp_inquiry_progress) a2dp_inquiry_progress(out, *n);
                if (done) return 1;
            }
        }
        drain_acl(hci);
    }
    return 0;
}

#define INQ_START_TRIES 6

static int inquiry_once(a2dp_session *s, a2dp_inq_dev *out, int max, int *nfound)
{
    unsigned char inq[5], cc[16];
    int cc_len = 0, seconds, length_slots, attempt, started = 0;
    int old_mode = -1, n = 0, r;
    hci_t hci;

    if (!s || !out || max <= 0 || !nfound) return 0;
    *nfound = 0;
    hci = s->hci;
    if (!hci.ops || !hci.ctx) return 0;

    seconds = s->opts.inquiry_seconds > 0 ? s->opts.inquiry_seconds : 10;
    /* Inquiry_Length unit = 1.28 s; valid 0x01..0x30. */
    length_slots = (int)((seconds * 100 + 64) / 128);
    if (a2dp_scan_deadline_ms) {
        /* Hard scan deadline: shorten this inquiry to end before it, or skip. */
        long left = a2dp_scan_deadline_ms - now_ms() - 500;
        int fit = (int)(left / 1280);
        if (fit < 1) {
            log_line("inquiry: scan deadline reached — not starting another");
            return 1;
        }
        if (length_slots > fit) length_slots = fit;
    }
    if (length_slots < 1) length_slots = 1;
    if (length_slots > 0x30) length_slots = 0x30;

    /* General/Unlimited Inquiry Access Code (GIAC) 0x9E8B33, LSB first;
     * Num_Responses 0 = unlimited. */
    inq[0] = 0x33; inq[1] = 0x8B; inq[2] = 0x9E;
    inq[3] = (unsigned char)length_slots;
    inq[4] = 0x00;

    /* Want RSSI/EIR results. Read the current mode; only if it is not
     * already 2 set it for our scan and put it back afterwards. */
    if (hci_cmd_sync(hci, 0x0C44, NULL, 0, cc, &cc_len, (int)sizeof cc) && cc_len >= 7)
        old_mode = cc[6];
    if (old_mode >= 0 && old_mode != 2) {
        unsigned char mode = 0x02;
        if (!hci_cmd_sync(hci, 0x0C45, &mode, 1, NULL, NULL, 0)) {
            log_line("inquiry: Write Inquiry Mode not accepted (mode %d kept)", old_mode);
            old_mode = -1;
        }
    }

    log_line("inquiry: starting (~%d s, length=%d, GIAC)", seconds, length_slots);
    for (attempt = 1; attempt <= INQ_START_TRIES && !started; attempt++) {
        int st = -1;
        if (!hci.ops->cmd(hci.ctx, HB_OP_INQUIRY, inq, 5)) {
            log_line("inquiry: transport error sending Inquiry");
            goto fail;
        }
        r = inquiry_listen(hci, out, max, &n, 2500, &st, HB_OP_INQUIRY);
        if (r < 0) goto lost;
        if (r == 3) { started = 1; goto aborted; }
        log_line("inquiry: Command Status for 0x0401: %s",
                 r == 2 ? (st == 0 ? "0x00 (started)" : "error") : r == 1 ? "none (complete seen)" :
                 r == 3 ? "aborted" : "none");
        if (r == 2 && st != 0) log_line("inquiry: 0x0401 status %#04x", st);
        if (r == 2 && st == 0) { started = 1; break; }
        if (r == 0 && n > 0) {          /* status lost but results flowing */
            log_line("inquiry: no cmd-status, results arriving — running");
            started = 1;
            break;
        }
        if (r == 2)
            log_line("inquiry: busy (cmd-status %#04x, try %d/%d)", st, attempt,
                     INQ_START_TRIES);
        else if (r == 1)
            log_line("inquiry: another inquiry just ended (try %d/%d)", attempt,
                     INQ_START_TRIES);
        else
            log_line("inquiry: no cmd-status (try %d/%d)", attempt, INQ_START_TRIES);
        /* 0x0C Command Disallowed / 0x0D / 0x3A busy: the system stack is
         * inquiring or paging. Its results reach us too; wait for its
         * Inquiry Complete (or a short back-off) and try again. */
        if (r == 2 || r == 0) {
            r = inquiry_listen(hci, out, max, &n, 1000 + attempt * 1000, NULL, 0);
            if (r < 0) goto lost;
            if (r == 3) goto aborted;
        }
    }
    if (!started) {
        if (n > 0) {
            log_line("inquiry: could not start ours; using %d result(s) seen", n);
        } else {
            log_line("inquiry: OP_INQUIRY failed after %d tries", INQ_START_TRIES);
            goto fail;
        }
    } else {
        {
            long lim = length_slots * 1280L + 3000;
            if (a2dp_scan_deadline_ms) {
                long cap = a2dp_scan_deadline_ms + 500 - now_ms();
                if (cap < 500) cap = 500;
                if (lim > cap) lim = cap;
            }
            r = inquiry_listen(hci, out, max, &n, (int)lim, NULL, 0);
        }
        if (r < 0) goto lost;
        if (r == 3) goto aborted;
        if (r == 0) {
            log_line("inquiry: no Inquiry Complete — cancelling ours");
            hci_cmd_sync(hci, HB_OP_INQUIRY_CANCEL, NULL, 0, NULL, NULL, 0);
            (void)inquiry_listen(hci, out, max, &n, 1000, NULL, 0);
        }
    }

    if (old_mode >= 0 && old_mode != 2) {
        unsigned char m = (unsigned char)old_mode;
        (void)hci_cmd_sync(hci, 0x0C45, &m, 1, NULL, NULL, 0);
    }

    log_line("inquiry: %d HCI event(s) during the inquiry window", g_inq_events);
    if (n > 0) fetch_names(s, out, n);
    {
        int i, audio_n = 0;
        for (i = 0; i < n; i++) {
            log_dev(&out[i]);
            if (out[i].audio_likely) audio_n++;
        }
        log_line("inquiry: found %d device(s), %d audio-likely", n, audio_n);
    }
    *nfound = n;
    return 1;

aborted:
    /* A page command is waiting: stop scanning so it runs right away. */
    log_line("inquiry: stopped early for a command (%d result(s))", n);
    if (started) {
        /* Wait for the cancel to complete, then let the controller settle
         * before the caller pages anyone. */
        long w;
        hci_cmd_sync(hci, HB_OP_INQUIRY_CANCEL, NULL, 0, NULL, NULL, 0);
        w = now_ms() + 500;
        while (now_ms() < w) {
            unsigned char ev[HCI_PKT_MAX];
            if (hci.ops->pump(hci.ctx, 40) < 0) break;
            while (hci.ops->next_event(hci.ctx, ev, (int)sizeof ev) > 0) { }
            drain_acl(hci);
        }
    }
    if (old_mode >= 0 && old_mode != 2) {
        unsigned char m = (unsigned char)old_mode;
        (void)hci_cmd_sync(hci, 0x0C45, &m, 1, NULL, NULL, 0);
    }
    *nfound = n;
    return 1;

lost:
    log_line("inquiry: transport lost / stop requested");
    if (started) hci.ops->cmd(hci.ctx, HB_OP_INQUIRY_CANCEL, NULL, 0);
fail:
    if (old_mode >= 0 && old_mode != 2) {
        unsigned char m = (unsigned char)old_mode;
        hci.ops->cmd(hci.ctx, 0x0C45, &m, 1);
    }
    return 0;
}

static const unsigned char k_evmask[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF, 0x7D };

/* Inquiry with recovery: when a whole inquiry brings no event at all (not
 * even its Command Status), the event path is re-armed (by the transport
 * pump), the event mask is sent again and the inquiry retried once. */
int a2dp_inquiry(a2dp_session *s, a2dp_inq_dev *out, int max, int *nfound)
{
    int ok;
    g_inq_events = 0;
    ok = inquiry_once(s, out, max, nfound);
    if (ok && *nfound == 0 && g_inq_events == 0 && !INQ_ABORTED() && s && s->hci.ops) {
        log_line("inquiry: no HCI events at all — re-sending the event mask and retrying");
        if (!hci_cmd_sync(s->hci, HB_OP_SET_EVENT_MASK, k_evmask, 8, NULL, NULL, 0))
            log_line("inquiry: SET_EVENT_MASK got no reply — event pipe looks stalled");
        if (s->hci.ops->diag) s->hci.ops->diag(s->hci.ctx);
        g_inq_events = 0;
        ok = inquiry_once(s, out, max, nfound);
        if (ok && *nfound == 0 && g_inq_events == 0)
            log_line("inquiry: still no events after recovery");
    }
    return ok;
}

/* ---- sink pick + Classic SSP pair (Core Vol 3 Part C 5.2.2) ------------ */

#define HEADSET_INI "/data/hearbridge/headset.ini"
#define T_PAIR_CONN_MS    8000
#define T_PAIR_SETUP_MS  10000

/* Only headphones and speakers are listed (hb_dev_rank): headphones / headsets first,
 * then speakers / portable audio, hands-free and headphone-like names. A2DP Sink support is confirmed later via SDP 0x110B. */
static int sink_rank(uint32_t cod, const char *name)
{
    return hb_dev_rank(cod, name);
}

static int prefer_set(const unsigned char prefer[6])
{
    int i;
    if (!prefer) return 0;
    for (i = 0; i < 6; i++) if (prefer[i]) return 1;
    return 0;
}

/* Scan log: each device's skip/candidate line once per scan, not on every
 * live update (a2dp_rank_log_reset() at the start of a scan). */
static unsigned char g_rank_logged[A2DP_INQ_MAX][6];
static unsigned char g_rank_kind[A2DP_INQ_MAX];   /* 1 skipped, 2 candidate */
static int g_rank_nlogged;
void a2dp_rank_log_reset(void) { g_rank_nlogged = 0; }
static int rank_log_once(const unsigned char addr[6], int kind)
{
    int i;
    for (i = 0; i < g_rank_nlogged; i++)
        if (!memcmp(g_rank_logged[i], addr, 6)) {
            if (g_rank_kind[i] == kind) return 0;
            g_rank_kind[i] = (unsigned char)kind;   /* e.g. a name arrived: log the change */
            return 1;
        }
    if (g_rank_nlogged < A2DP_INQ_MAX) {
        memcpy(g_rank_logged[g_rank_nlogged], addr, 6);
        g_rank_kind[g_rank_nlogged++] = (unsigned char)kind;
    }
    return 1;
}

int a2dp_rank_sinks(const a2dp_inq_dev *found, int n,
                    const unsigned char prefer_addr[6], int *order, int max)
{
    int i, j, cnt = 0, rk[A2DP_INQ_MAX];

    if (!found || n <= 0 || !order || max <= 0) return 0;
    if (n > A2DP_INQ_MAX) n = A2DP_INQ_MAX;
    for (i = 0; i < n && cnt < max; i++) {
        int r = sink_rank(found[i].cod, found[i].name);
        if (r < 0) {
            if (rank_log_once(found[i].addr, 1)) {
                char astr[18];
                hci_addr_str(found[i].addr, astr);
                log_line("pick: skip %s CoD %06x (not listed: %s)", astr, (unsigned)found[i].cod,
                         found[i].name[0] ? "not headphones/speaker" : "no name yet");
            }
            continue;
        }
        if (prefer_set(prefer_addr) && same_addr(found[i].addr, prefer_addr))
            r = -2; /* preferred addr first (still must pass the filter) */
        rk[cnt] = r;
        order[cnt++] = i;
    }
    /* Stable sort: rank, then strongest RSSI, then discovery order. */
    for (i = 1; i < cnt; i++) {
        for (j = i; j > 0; j--) {
            const a2dp_inq_dev *x = &found[order[j]], *y = &found[order[j - 1]];
            int better = rk[j] < rk[j - 1] ||
                (rk[j] == rk[j - 1] && x->have_rssi && y->have_rssi &&
                 x->rssi > y->rssi);
            if (!better) break;
            { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
            { int t = rk[j]; rk[j] = rk[j - 1]; rk[j - 1] = t; }
        }
    }
    for (i = 0; i < cnt; i++)
        if (rank_log_once(found[order[i]].addr, 2))
        log_line("pick: candidate %d = #%d CoD %06x \"%s\"%s", i, order[i],
                 (unsigned)found[order[i]].cod,
                 found[order[i]].name[0] ? found[order[i]].name : "-",
                 rk[i] == -2 ? " (headset.ini addr)" : "");
    if (!cnt && rank_log_once((const unsigned char *)"\0\0\0\0\0\0", 3))
        log_line("pick: no audio device yet (pairing mode?)");
    return cnt;
}

int a2dp_pick_sink(const a2dp_inq_dev *found, int n,
                   const unsigned char prefer_addr[6])
{
    int order[A2DP_INQ_MAX];
    return a2dp_rank_sinks(found, n, prefer_addr, order, A2DP_INQ_MAX) > 0
           ? order[0] : -1;
}

static void fire_cmd(hci_t hci, unsigned op, const void *params, int plen)
{
    if (hci.ops && hci.ops->cmd)
        hci.ops->cmd(hci.ctx, op, params, plen);
}

/* ACL already exists (0x0b): the link belongs to the system stack. Never
 * disconnect guessed handles (that dropped the paired controller); just
 * let the caller fail/retry later. */
static int purge_stale_handles(hci_t hci)
{
    (void)hci;
    log_line("pair: ACL to this device is held by the system — not dropping it");
    return -1;
}

static int save_headset_ini(const a2dp_pair_result *r)
{
    FILE *f;
    char astr[18];
    int i;

    f = fopen(HEADSET_INI, "w");
    if (!f) {
        log_line("pair: cannot write %s", HEADSET_INI);
        return 0;
    }
    hci_addr_str(r->addr, astr);
    fprintf(f, "addr=%s\n", astr);
    fprintf(f, "name=%s\n", r->name[0] ? r->name : "");
    fprintf(f, "cod=%06x\n", (unsigned)r->cod);
    fprintf(f, "key_type=%u\n", (unsigned)r->key_type);
    fprintf(f, "key=");
    for (i = 0; i < 16; i++) fprintf(f, "%02x", r->link_key[i]);
    fprintf(f, "\n");
    fclose(f);
    log_line("pair: saved %s", HEADSET_INI);
    return 1;
}

int a2dp_pair_keep_acl;

int a2dp_pair(a2dp_session *s, const a2dp_inq_dev *target, a2dp_pair_result *out)
{
    hci_t hci;
    unsigned char p[32];
    unsigned char ev[HCI_PKT_MAX];
    char astr[18];
    long t0, deadline;
    int connected = 0, auth_ok = 0, enc_on = 0, key_ok = 0;
    int auth_sent = 0, enc_sent = 0, create_sent = 0;
    int handle = -1;
    int create_status_ok = 0;
    long t_conn = 0, t_auth_ok = 0;
    unsigned char link_key[16];
    unsigned char key_type = 0;
    int purged = 0;
    int rc = 0;

    if (!s || !target || !out) return 0;
    memset(out, 0, sizeof *out);
    memcpy(out->addr, target->addr, 6);
    out->cod = target->cod;
    out->handle = -1;
    if (target->name[0])
        snprintf(out->name, sizeof out->name, "%s", target->name);

    hci = s->hci;
    if (!hci.ops || !hci.ctx) return 0;

    hci_addr_str(target->addr, astr);
    log_line("pair: connecting to %s \"%s\" CoD %06x",
             astr, out->name[0] ? out->name : "-", (unsigned)target->cod);

    {
        /* The radio must be idle before paging: an inquiry still running or
         * a Remote Name Request to this device (a temporary baseband link)
         * makes Create Connection fail with 0x0b. Cancel both, wait for
         * their completion, settle. */
        long w;
        (void)hci_cmd_sync(hci, HB_OP_INQUIRY_CANCEL, NULL, 0, NULL, NULL, 0);
        (void)hci_cmd_sync(hci, 0x041A, target->addr, 6, NULL, NULL, 0);
        w = now_ms() + 300;
        while (now_ms() < w) {
            if (hci.ops->pump(hci.ctx, 30) < 0) break;
            while (hci.ops->next_event(hci.ctx, ev, (int)sizeof ev) > 0) { }
            drain_acl(hci);
        }
    }

    /* Create Connection: addr6, pkt_type, psrm, reserved, clock, role_switch */
    memcpy(p, target->addr, 6);
    put16(p + 6, 0xCC18); /* DM1..DH5 */
    p[8] = target->psrm ? target->psrm : 0x01;
    p[9] = 0;
    if (target->clock_offset & 0x8000)
        put16(p + 10, target->clock_offset);
    else
        put16(p + 10, 0);
    p[12] = 1; /* allow role switch */
    log_line("pair: CREATE_CONNECTION params %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12]);

    if (!hci.ops->cmd(hci.ctx, HB_OP_CREATE_CONNECTION, p, 13)) {
        log_line("pair: CREATE_CONNECTION transport fail");
        return 0;
    }
    create_sent = 1;
    t0 = now_ms();
    deadline = t0 + T_PAIR_CONN_MS + T_PAIR_SETUP_MS;

    while (now_ms() < deadline) {
        int nEv, pr;
        long now = now_ms();

        pr = hci.ops->pump(hci.ctx, 40);
        if (pr < 0) {
            log_line("pair: transport lost");
            break;
        }

        while ((nEv = hci.ops->next_event(hci.ctx, ev, (int)sizeof ev)) > 0) {
            /* Command Status */
            if (ev[0] == 0x0F && nEv >= 6) {
                unsigned rop = (unsigned)ev[4] | ((unsigned)ev[5] << 8);
                if (rop == HB_OP_CREATE_CONNECTION) {
                    if (ev[2] == 0) {
                        create_status_ok = 1;
                        log_line("pair: CREATE_CONNECTION accepted");
                    } else if (ev[2] == 0x0B && !purged) {
                        log_line("pair: ACL already exists — drop + retry once");
                        purged = 1;
                        if (purge_stale_handles(hci) < 0) {
                            log_line("pair: drop aborted — FAIL cleanly");
                            goto done;
                        }
                        {
                            long w = now_ms() + 1000;
                            (void)hci_cmd_sync(hci, 0x041A, target->addr, 6, NULL, NULL, 0);
                            while (now_ms() < w) {
                                hci.ops->pump(hci.ctx, 20);
                                drain_acl(hci);
                            }
                        }
                        if (hci.ops->cmd(hci.ctx, HB_OP_CREATE_CONNECTION, p, 13))
                            create_sent = 1;
                    } else {
                        log_line("pair: CREATE_CONNECTION cmd-status %#04x", ev[2]);
                        goto done;
                    }
                } else if (rop == HB_OP_AUTH_REQUESTED && ev[2] != 0) {
                    log_line("pair: AUTH cmd-status %#04x — will retry", ev[2]);
                    auth_sent = 0;
                    t_conn = now_ms();
                } else if (rop == HB_OP_SET_ENCRYPTION && ev[2] != 0) {
                    log_line("pair: SET_ENCRYPTION cmd-status %#04x", ev[2]);
                }
                continue;
            }

            /* Connection Complete: status, handle, addr, link_type, enc */
            if (ev[0] == 0x03 && nEv >= 13 && same_addr(ev + 5, target->addr)) {
                if (ev[2] != 0) {
                    log_line("pair: Connection Complete fail status %#04x", ev[2]);
                    goto done;
                }
                handle = (int)(le16(ev + 3) & 0x0FFF);
                connected = 1;
                t_conn = now_ms();
                out->handle = handle;
                log_line("pair: ACL up handle %#05x", handle);
                continue;
            }

            /* Disconnection Complete */
            if (ev[0] == 0x05 && nEv >= 6 && connected &&
                (int)(le16(ev + 3) & 0x0FFF) == handle) {
                log_line("pair: disconnected early (reason %#04x)", ev[5]);
                connected = 0;
                goto done;
            }

            /* Authentication Complete */
            if (ev[0] == 0x06 && nEv >= 5 && connected &&
                (int)(le16(ev + 3) & 0x0FFF) == handle) {
                if (ev[2] == 0) {
                    auth_ok = 1;
                    t_auth_ok = now_ms();
                    log_line("pair: Authentication Complete OK");
                } else {
                    log_line("pair: Authentication Complete fail %#04x", ev[2]);
                    goto done;
                }
                continue;
            }

            /* Encryption Change */
            if (ev[0] == 0x08 && nEv >= 6 && connected &&
                (int)(le16(ev + 3) & 0x0FFF) == handle) {
                if (ev[2] == 0 && ev[5]) {
                    enc_on = 1;
                    auth_ok = 1;
                    log_line("pair: Encryption Change enabled");
                } else {
                    log_line("pair: Encryption Change status %#04x en=%u",
                             ev[2], nEv > 5 ? ev[5] : 0);
                }
                continue;
            }

            /* PIN Code Request — legacy fallback "0000" */
            if (ev[0] == 0x16 && nEv >= 8 && same_addr(ev + 2, target->addr)) {
                unsigned char pin[23] = { 0 };
                int q;
                for (q = 0; q < 6; q++) pin[q] = ev[2 + q];
                pin[6] = 4;               /* PIN length */
                pin[7] = pin[8] = pin[9] = pin[10] = '0';
                fire_cmd(hci, HB_OP_PIN_CODE_REPLY, pin, (int)sizeof pin);
                log_line("pair: PIN Code Reply 0000");
                continue;
            }

            /* Link Key Request — no stored key yet → Neg Reply (new SSP) */
            if (ev[0] == 0x17 && nEv >= 8 && same_addr(ev + 2, target->addr)) {
                fire_cmd(hci, HB_OP_LINK_KEY_NEG_REPLY, ev + 2, 6);
                log_line("pair: Link Key Neg Reply (fresh pair)");
                continue;
            }

            /* Link Key Notification */
            if (ev[0] == 0x18 && nEv >= 25 && same_addr(ev + 2, target->addr)) {
                memcpy(link_key, ev + 8, 16);
                key_type = ev[24];
                key_ok = 1;
                log_line("pair: Link Key Notification type=%u", key_type);
                continue;
            }

            /* IO Capability Request */
            if (ev[0] == 0x31 && nEv >= 8 && same_addr(ev + 2, target->addr)) {
                static const unsigned char iocap[3] = {
                    0x03,   /* IO capability: NoInputNoOutput */
                    0x00,   /* OOB data not present */
                    0x04    /* auth req: General Bonding, MITM not required */
                };
                unsigned char rep[9];
                int q;
                for (q = 0; q < 6; q++) rep[q] = ev[2 + q];
                for (q = 0; q < 3; q++) rep[6 + q] = iocap[q];
                fire_cmd(hci, HB_OP_IO_CAP_REPLY, rep, (int)sizeof rep);
                log_line("pair: IO Cap Reply (NoInputNoOutput)");
                continue;
            }

            /* User Confirmation Request */
            if (ev[0] == 0x33 && nEv >= 8 && same_addr(ev + 2, target->addr)) {
                fire_cmd(hci, HB_OP_USER_CONFIRM_REPLY, ev + 2, 6);
                log_line("pair: User Confirmation Reply");
                continue;
            }
        }
        /* Keeping the link: leave the peer's L2CAP packets (an early SDP
         * or info request) queued for btlink instead of discarding them. */
        if (!(a2dp_pair_keep_acl && connected)) drain_acl(hci);

        /* Drive auth / encrypt per link state (pairing: no delay). */
        if (connected && !auth_ok && !auth_sent) {
            put16(p, (unsigned)handle);
            fire_cmd(hci, HB_OP_AUTH_REQUESTED, p, 2);
            auth_sent = 1;
            log_line("pair: Authentication Requested");
        }
        if (connected && auth_ok && !enc_on && !enc_sent) {
            put16(p, (unsigned)handle);
            p[2] = 1;
            fire_cmd(hci, HB_OP_SET_ENCRYPTION, p, 3);
            enc_sent = 1;
            log_line("pair: Set Connection Encryption");
        }

        if (connected && enc_on && key_ok) {
            rc = 1;
            break;
        }

        /* Timeouts */
        if (create_sent && !connected && now - t0 > T_PAIR_CONN_MS) {
            log_line("pair: connection timeout");
            break;
        }
        if (connected && now - t_conn > T_PAIR_SETUP_MS) {
            log_line("pair: setup timeout (auth=%d enc=%d key=%d)",
                     auth_ok, enc_on, key_ok);
            break;
        }
        (void)create_status_ok;
        (void)t_auth_ok;
    }

done:
    if (rc) {
        memcpy(out->link_key, link_key, 16);
        out->key_type = key_type;
        out->paired = 1;
        out->encrypted = 1;
        save_headset_ini(out);
        log_line("pair: PASS — encrypted + link key stored");
    } else {
        log_line("pair: FAIL — connected=%d auth=%d enc=%d key=%d",
                 connected, auth_ok, enc_on, key_ok);
        /* Soft success: encrypted without notification is rare; still fail. */
        if (connected && enc_on && !key_ok)
            log_line("pair: encrypted but no Link Key Notification");
    }

    if (rc && connected && enc_on && handle >= 0 && a2dp_pair_keep_acl) {
        out->handle = handle;
        log_line("pair: keeping the encrypted ACL %#05x for SDP/AVDTP", handle);
        return rc;
    }
    if (connected && handle >= 0) {
        put16(p, (unsigned)handle);
        p[2] = 0x13;
        fire_cmd(hci, HB_OP_DISCONNECT, p, 3);
        {
            long w = now_ms() + 1500;
            while (now_ms() < w) {
                int nEv;
                if (hci.ops->pump(hci.ctx, 40) < 0) break;
                while ((nEv = hci.ops->next_event(hci.ctx, ev, (int)sizeof ev)) > 0) {
                    if (ev[0] == 0x05) break;
                }
                drain_acl(hci);
            }
        }
        log_line("pair: ACL closed");
    }

    return rc;
}
