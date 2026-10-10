/* hfp.c - HFP Audio Gateway for the headset battery (see hfp.h).
 * Developed by X-F1REBALL-X. */
#include "hfp.h"

#include <stdio.h>
#include <string.h>

#define RFC_MAX_FRAME 127          /* our RFCOMM max frame size (1-byte length) */
#define RFC_CREDITS   7            /* credits we hand the headset */

/* Mux control message types (TS 07.10 5.4.6.3), C/R and EA bits cleared. */
#define MUX_PN    0x80
#define MUX_MSC   0xE0
#define MUX_RPN   0x90
#define MUX_RLS   0x50
#define MUX_TEST  0x20
#define MUX_FCON  0xA0
#define MUX_FCOFF 0x60
#define MUX_NSC   0x10

/* CRC-8, polynomial x^8 + x^2 + x + 1, reflected (TS 07.10 annex B). */
unsigned char rfc_fcs(const unsigned char *p, int n)
{
    unsigned char f = 0xFF;
    int i, b;
    for (i = 0; i < n; i++) {
        f ^= p[i];
        for (b = 0; b < 8; b++) f = (unsigned char)((f & 1) ? (f >> 1) ^ 0xE0 : f >> 1);
    }
    return (unsigned char)(0xFF - f);
}

int rfc_build(unsigned char *out, int max, int dlci, int cr, int type, int pf,
              int credits, const unsigned char *info, int len)
{
    int n = 0, hdr;
    if (len < 0 || len > 32767 || max < len + 6) return 0;
    out[n++] = (unsigned char)((dlci << 2) | (cr ? 2 : 0) | 1);
    out[n++] = (unsigned char)(type | (pf ? RFC_PF : 0));
    if (len <= 127) out[n++] = (unsigned char)((len << 1) | 1);
    else { out[n++] = (unsigned char)(len << 1); out[n++] = (unsigned char)(len >> 7); }
    hdr = n;
    if (credits >= 0) out[n++] = (unsigned char)credits;
    if (len) memcpy(out + n, info, (size_t)len);
    n += len;
    out[n] = rfc_fcs(out, (type & ~RFC_PF) == RFC_UIH ? 2 : hdr);
    return n + 1;
}

int rfc_parse(const unsigned char *d, int len, int cfc, rfc_frame *f)
{
    int hdr, n;
    if (len < 4 || !(d[0] & 1)) return -1;
    f->dlci = d[0] >> 2;
    f->cr = (d[0] >> 1) & 1;
    f->pf = (d[1] & RFC_PF) != 0;
    f->type = d[1] & ~RFC_PF;
    if (d[2] & 1) { n = d[2] >> 1; hdr = 3; }
    else { if (len < 5) return -1; n = (d[2] >> 1) | (d[3] << 7); hdr = 4; }
    if (rfc_fcs(d, f->type == RFC_UIH ? 2 : hdr) != d[len - 1]) return -1;
    f->credits = -1;
    if (f->type == RFC_UIH && f->pf && f->dlci && cfc) {
        if (hdr + 1 >= len) return -1;
        f->credits = d[hdr++];
    }
    if (hdr + n + 1 != len) return -1;
    f->info = d + hdr;
    f->len = n;
    return 0;
}

static void say(hfp_state *h, const char *msg)
{
    if (h->log) h->log(msg);
}

void hfp_init(hfp_state *h, hfp_send_fn send, void *ud, hfp_log_fn log)
{
    memset(h, 0, sizeof *h);
    h->send = send;
    h->ud = ud;
    h->log = log;
    h->battery = -1;
    h->mtu = RFC_MAX_FRAME;
}

/* C/R bit of our commands / responses: the headset is normally the
 * initiator, so our commands carry 0 and our responses 1. */
static int cr_cmd(const hfp_state *h) { return h->peer_init ? 0 : 1; }
static int cr_rsp(const hfp_state *h) { return h->peer_init ? 1 : 0; }

static void tx(hfp_state *h, int dlci, int cr, int type, int pf, int credits,
               const unsigned char *info, int len)
{
    unsigned char f[RFC_MAX_FRAME + 8];
    int n = rfc_build(f, (int)sizeof f, dlci, cr, type, pf, credits, info, len);
    if (n > 0 && h->send) h->send(h->ud, f, n);
}

static void mux_send(hfp_state *h, int mtype, int is_cmd, const unsigned char *v, int n)
{
    unsigned char m[16];
    if (n > 12) return;
    m[0] = (unsigned char)(mtype | (is_cmd ? 2 : 0) | 1);
    m[1] = (unsigned char)((n << 1) | 1);
    memcpy(m + 2, v, (size_t)n);
    tx(h, 0, cr_cmd(h), RFC_UIH, 0, -1, m, n + 2);
}

/* Hand the headset more credits once it is running low. */
static int credits_due(hfp_state *h)
{
    if (!h->cfc || h->rx_credits > 2) return -1;
    {
        int give = RFC_CREDITS - h->rx_credits;
        h->rx_credits += give;
        return give;
    }
}

/* Send queued AT replies while credits last (one frame each). */
static void flush(hfp_state *h)
{
    while (h->dlci_up && h->outq_n > 0 && (!h->cfc || h->tx_credits > 0)) {
        int n = h->outq_n < h->mtu ? h->outq_n : h->mtu, cr = credits_due(h);
        tx(h, h->dlci, cr_cmd(h), RFC_UIH, cr >= 0, cr, h->outq, n);
        if (h->cfc) h->tx_credits--;
        memmove(h->outq, h->outq + n, (size_t)(h->outq_n - n));
        h->outq_n -= n;
    }
}

static void put_text(hfp_state *h, const char *s, int n)
{
    if (n > (int)sizeof h->outq - h->outq_n) n = (int)sizeof h->outq - h->outq_n;
    memcpy(h->outq + h->outq_n, s, (size_t)n);
    h->outq_n += n;
}

/* ---------- AT commands ---------- */

static int starts(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

/* Read a decimal number at *s, move past it and one ',' after it. */
static int num(const char **s, int *v)
{
    const char *p = *s;
    int x = 0, any = 0;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9' && x < 100000) { x = x * 10 + (*p - '0'); p++; any = 1; }
    while (*p == ' ') p++;
    if (*p == ',') p++;
    *s = p;
    *v = x;
    return any;
}

static void set_battery(hfp_state *h, int pct, int src)
{
    char m[64];
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct != h->battery || src != h->battery_src) {
        snprintf(m, sizeof m, "hfp: headset battery %d%%", pct);
        say(h, m);
    }
    h->battery = pct;
    h->battery_src = src;
    h->battery_seq++;
}

#define OK_ "\r\nOK\r\n"

int hfp_at(hfp_state *h, const char *in, char *out, int max)
{
    char c[sizeof h->line];
    const char *a;
    int i, v, k;

    for (i = 0; in[i] && i < (int)sizeof c - 1; i++)
        c[i] = (in[i] >= 'a' && in[i] <= 'z') ? (char)(in[i] - 32) : in[i];
    c[i] = 0;

    if (starts(c, "AT+BRSF=")) {
        a = c + 8;
        if (num(&a, &v)) h->hf_features = v;
        return snprintf(out, (size_t)max, "\r\n+BRSF: %d\r\n" OK_, HFP_AG_FEATURES);
    }
    if (!strcmp(c, "AT+CIND=?"))
        return snprintf(out, (size_t)max, "\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),"
                        "(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),"
                        "(\"roam\",(0,1)),(\"battchg\",(0-5))\r\n" OK_);
    if (!strcmp(c, "AT+CIND?"))
        return snprintf(out, (size_t)max, "\r\n+CIND: 1,0,0,0,5,0,5\r\n" OK_);
    if (starts(c, "AT+CMER=")) {
        if (!h->slc) say(h, "hfp: headset linked (battery only, no calls)");
        h->slc = 1;
        return snprintf(out, (size_t)max, OK_);
    }
    if (!strcmp(c, "AT+CHLD=?"))
        return snprintf(out, (size_t)max, "\r\n+CHLD: (0,1,2,3)\r\n" OK_);
    if (!strcmp(c, "AT+BIND=?"))
        return snprintf(out, (size_t)max, "\r\n+BIND: (2)\r\n" OK_);
    if (!strcmp(c, "AT+BIND?"))
        return snprintf(out, (size_t)max, "\r\n+BIND: 2,1\r\n" OK_);
    if (starts(c, "AT+BIND=")) {
        a = c + 8;
        while (num(&a, &v)) if (v == 2) h->bind_batt = 1;
        return snprintf(out, (size_t)max, OK_);
    }
    if (starts(c, "AT+BIEV=")) {
        a = c + 8;
        if (num(&a, &k) && num(&a, &v) && k == 2) set_battery(h, v, 1);
        return snprintf(out, (size_t)max, OK_);
    }
    if (starts(c, "AT+XAPL="))   /* Apple accessory: features bit 1 = battery */
        return snprintf(out, (size_t)max, "\r\n+XAPL=iPhone,2\r\n" OK_);
    if (starts(c, "AT+IPHONEACCEV=")) {
        int n;
        a = c + 15;
        if (num(&a, &n))
            for (i = 0; i < n && num(&a, &k) && num(&a, &v); i++)
                /* 0-9 = 10 % steps: only while no exact HF indicator
                 * (AT+BIEV, 0-100) has come from this headset. */
                if (k == 1 && v >= 0 && v <= 9 && h->battery_src != 1) set_battery(h, (v + 1) * 10, 2);
        return snprintf(out, (size_t)max, OK_);
    }
    if (!strcmp(c, "AT+COPS?"))
        return snprintf(out, (size_t)max, "\r\n+COPS: 0,0,\"HearBridge\"\r\n" OK_);
    /* Anything that would start a call or call audio: refused. */
    if (!strcmp(c, "ATA") || starts(c, "ATD") || starts(c, "AT+BCC") || starts(c, "AT+BCS") ||
        starts(c, "AT+BVRA=1") || starts(c, "AT+BLDN"))
        return snprintf(out, (size_t)max, "\r\nERROR\r\n");
    return snprintf(out, (size_t)max, OK_);
}

static void at_bytes(hfp_state *h, const unsigned char *d, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        char ch = (char)d[i];
        if (ch == '\r' || ch == '\n') {
            if (h->line_n) {
                char r[400];
                int m;
                h->line[h->line_n] = 0;
                m = hfp_at(h, h->line, r, (int)sizeof r);
                if (m > (int)sizeof r - 1) m = (int)sizeof r - 1;
                if (m > 0) put_text(h, r, m);
                h->line_n = 0;
            }
        } else if (h->line_n < (int)sizeof h->line - 1) {
            h->line[h->line_n++] = ch;
        }
    }
    flush(h);
}

/* ---------- RFCOMM ---------- */

static void on_mux(hfp_state *h, const unsigned char *m, int n)
{
    while (n >= 2) {
        int type = m[0] & 0xFC, is_cmd = (m[0] >> 1) & 1, vl, hl = 2;
        const unsigned char *v;
        if (m[1] & 1) vl = m[1] >> 1;
        else { if (n < 3) return; vl = (m[1] >> 1) | (m[2] << 7); hl = 3; }
        if (hl + vl > n) return;
        v = m + hl;
        if (!is_cmd) {                              /* answers to our MSC etc. */
            m += hl + vl; n -= hl + vl;
            continue;
        }
        switch (type) {
        case MUX_PN:
            if (vl >= 8) {
                unsigned char r[8];
                int fs = v[4] | (v[5] << 8);
                if (fs <= 0 || fs > RFC_MAX_FRAME) fs = RFC_MAX_FRAME;
                h->cfc = (v[1] >> 4) == 0xF;
                h->tx_credits = v[7] & 7;
                h->mtu = fs;
                r[0] = v[0] & 0x3F;
                r[1] = (unsigned char)(h->cfc ? 0xE0 : 0x00);
                r[2] = v[2];
                r[3] = 0;
                r[4] = (unsigned char)fs; r[5] = (unsigned char)(fs >> 8);
                r[6] = 0;
                r[7] = (unsigned char)(h->cfc ? RFC_CREDITS : 0);
                h->rx_credits = h->cfc ? RFC_CREDITS : 0;
                mux_send(h, MUX_PN, 0, r, 8);
            }
            break;
        case MUX_MSC:
            mux_send(h, MUX_MSC, 0, v, vl > 3 ? 3 : vl);
            break;
        case MUX_RPN:
            if (vl == 1) {     /* query: 9600 8N1, no flow control */
                unsigned char r[8] = { v[0], 0x03, 0x03, 0x00, 0x11, 0x13, 0xFF, 0x3F };
                mux_send(h, MUX_RPN, 0, r, 8);
            } else {
                mux_send(h, MUX_RPN, 0, v, vl > 8 ? 8 : vl);
            }
            break;
        case MUX_RLS:
        case MUX_TEST:
        case MUX_FCON:
        case MUX_FCOFF:
            mux_send(h, type, 0, v, vl > 12 ? 12 : vl);
            break;
        default: {
            unsigned char r[1];
            r[0] = m[0];
            mux_send(h, MUX_NSC, 0, r, 1);
            break;
        }
        }
        m += hl + vl; n -= hl + vl;
    }
}

void hfp_input(hfp_state *h, const unsigned char *d, int len)
{
    rfc_frame f;
    if (rfc_parse(d, len, h->cfc, &f) != 0) {
        say(h, "hfp: bad RFCOMM frame dropped");
        return;
    }
    switch (f.type) {
    case RFC_SABM:
        if (f.dlci == 0) {
            h->peer_init = f.cr;
            h->mux_up = 1;
            tx(h, 0, cr_rsp(h), RFC_UA, 1, -1, NULL, 0);
        } else if (h->mux_up && (f.dlci >> 1) == HFP_RFCOMM_CHANNEL && !h->dlci_up) {
            unsigned char msc[2];
            h->dlci = f.dlci;
            h->dlci_up = 1;
            tx(h, f.dlci, cr_rsp(h), RFC_UA, 1, -1, NULL, 0);
            msc[0] = (unsigned char)((f.dlci << 2) | 2 | 1);
            msc[1] = 0x8D;                        /* RTC, RTR, DV */
            mux_send(h, MUX_MSC, 1, msc, 2);
            say(h, "hfp: headset opened the hands-free channel");
        } else {
            tx(h, f.dlci, cr_rsp(h), RFC_DM, 1, -1, NULL, 0);
        }
        break;
    case RFC_DISC:
        tx(h, f.dlci, cr_rsp(h), RFC_UA, 1, -1, NULL, 0);
        if (f.dlci == 0) {
            hfp_send_fn s = h->send;
            void *ud = h->ud;
            hfp_log_fn lg = h->log;
            int b = h->battery, bs = h->battery_seq, src = h->battery_src;
            if (h->dlci_up) say(h, "hfp: headset closed the hands-free channel");
            hfp_init(h, s, ud, lg);
            h->battery = b; h->battery_seq = bs; h->battery_src = src;   /* keep the last value */
        } else if (f.dlci == h->dlci && h->dlci_up) {
            say(h, "hfp: headset closed the hands-free channel");
            h->dlci = 0; h->dlci_up = 0; h->slc = 0;
            h->outq_n = 0; h->line_n = 0;
        }
        break;
    case RFC_UIH:
        if (f.dlci == 0) { on_mux(h, f.info, f.len); break; }
        if (f.dlci != h->dlci || !h->dlci_up) break;
        if (f.credits > 0) h->tx_credits += f.credits;
        if (f.len > 0) {
            if (h->cfc && h->rx_credits > 0) h->rx_credits--;
            at_bytes(h, f.info, f.len);
            if (h->cfc && h->rx_credits <= 2) {   /* nothing to answer with: plain credits */
                int give = credits_due(h);
                if (give > 0) tx(h, h->dlci, cr_cmd(h), RFC_UIH, 1, give, NULL, 0);
            }
        } else {
            flush(h);
        }
        break;
    case RFC_UA:
    case RFC_DM:
        /* Answers to our own close. */
        if (h->closing == 1 && f.dlci == h->dlci) {
            h->dlci_up = 0; h->slc = 0; h->outq_n = 0; h->line_n = 0;
            h->closing = 2;
            tx(h, 0, cr_cmd(h), RFC_DISC, 1, -1, NULL, 0);
        } else if (h->closing == 2 && f.dlci == 0) {
            h->mux_up = 0; h->dlci = 0; h->closing = 0;
            say(h, "hfp: hands-free channel closed");
        }
        break;
    default:
        break;
    }
}

int hfp_close(hfp_state *h)
{
    if (!h->mux_up) return 0;
    if (h->dlci_up) {
        h->closing = 1;
        tx(h, h->dlci, cr_cmd(h), RFC_DISC, 1, -1, NULL, 0);
    } else {
        h->closing = 2;
        tx(h, 0, cr_cmd(h), RFC_DISC, 1, -1, NULL, 0);
    }
    return 1;
}

int hfp_closed(const hfp_state *h)
{
    return !h->mux_up && !h->dlci_up;
}
