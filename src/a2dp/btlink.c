#include "btlink.h"
#include "acl_track.h"
#include "acl_pool.h"
#include "avrcp.h"
#include "sdp_server.h"
#include "hci_cmd.h"
#include "log.h"
#include "util.h"
#include "rate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* L2CAP signaling command codes (Core Vol 3 Part A, 4). */
enum {
    L2SIG_REJECT = 0x01, L2SIG_CONN_REQ, L2SIG_CONN_RSP,
    L2SIG_CFG_REQ, L2SIG_CFG_RSP, L2SIG_DISC_REQ, L2SIG_DISC_RSP,
    L2SIG_ECHO_REQ, L2SIG_ECHO_RSP, L2SIG_INFO_REQ, L2SIG_INFO_RSP
};

#define T_CHAN_CONNECT 4000
#define T_CFG_TIMEOUT  8000
#define T_CFG_TIMEOUT_MEDIA 16000  /* second PSM 0x19 — some sinks are slow/racy */
#define T_CFG_RESEND   1000
#define T_CFG_DEFER_MS 40         /* wait briefly for peer CFG before ours (media) */
#define CFG_TRIES      10
#define CFG_OPT_MAX    16

typedef enum { CH_CLOSED = 0, CH_CONNECTING, CH_CONFIG, CH_OPEN } chan_state;

typedef struct {
    chan_state st;
    unsigned psm;
    unsigned scid, dcid;   /* our CID, remote CID */
    int inbound; /* 1 = remote opened to us (SDP client is them) */
    int cfg_ours_ok, cfg_theirs_ok, cfg_tries;
    int cfg_want_mtu; /* 1 = next CFG_REQ includes MTU */
    unsigned cfg_mtu; /* MTU to offer when cfg_want_mtu (default 672) */
    unsigned peer_mtu; /* MTU from peer CFG_REQ, if any */
    int cfg_deferred; /* media: delay our CFG_REQ until peer CFG or timeout */
    int cfg_nudged;   /* re-sent our CFG_REQ once after a silent peer */
    long t_state, t_cfg;
    btlink_rx_fn rx;
    void *rx_ud;
    /* one-shot wait buffer */
    unsigned char *wait_buf;
    int wait_max, wait_len, wait_armed;
    /* P2: CFG_REQ arrived before we have dcid — queue id+opts, flush after CONN_RSP */
    int pending_cfg;
    unsigned char pending_cfg_id;
    unsigned char pending_cfg_opt[CFG_OPT_MAX];
    int pending_cfg_optlen;
} chan;

/* Controller ACL flow control (Core Vol 4 Part E 4.1.1). The controller's
 * buffers are shared with the system stack (DualSense traffic), so we use
 * at most (buffers - ACL_RESERVE) and get credits back from Number Of
 * Completed Packets for OUR handle only. If those events stop reaching us
 * (the system stack also reads the event pipe), a slow timed refill
 * keeps the link moving. */
#define ACL_RESERVE      1     /* one controller buffer kept for the system (DualSense) */
#define TXQ_MAX          72
#define MEDIA_Q_MAX      64    /* hard ceiling for the per-link media queue */
#define MEDIA_Q_DEFAULT  40    /* start: ~1 s at 11 frames/packet; tuned per link */

typedef struct {
    uint16_t len;
    unsigned char media;      /* A2DP media: may be dropped when late */
    long due;                 /* media: earliest send time (pacing) */
    unsigned char d[HCI_PKT_MAX];
} txq_item;

struct btlink {
    hci_t hci;
    avrcp_state avrcp;
    unsigned avrcp_scid;      /* open AVRCP control channel (either side) */
    unsigned in_rx_psm;
    btlink_rx_fn in_rx;
    void *in_rx_ud;
    /* AVDTP the headset sent on a channel it opened, before anyone listens:
     * kept and replayed when the handler is installed (never dropped). */
    struct { unsigned scid; int len; unsigned char d[160]; } early[6];
    int n_early;
    long cfg_timeout_ms;      /* 0 = default; non-media channel config limit */
    int connected;
    unsigned handle;
    unsigned char addr[6];
    int auth_sent, auth_ok, enc_sent, enc_on;
    long t_conn;
    unsigned char link_key[16];
    unsigned char key_type;
    int have_key;           /* reply with stored key */
    int fresh_pair;         /* Neg Reply on Link Key Request */
    char name[64];

    unsigned acl_mtu;
    acl_pool pool;
    txq_item txq[TXQ_MAX];
    int txq_head, txq_n, txq_media;
    int tx_is_media;          /* set around a media send */
    int media_cap;            /* media queue depth for this link (tuned) */
    long media_dur_ms;        /* audio length of the next media packet */
    long media_clock;         /* pacing clock: due time of the next media packet */
    long cur_due;
    int sniff_seen;
    unsigned char sig_id;

    chan ch[BTLINK_CHAN_MAX];
    unsigned next_scid;

    unsigned char rx[BTLINK_RX_MAX];
    int rx_len, rx_need;

    int dead;
    int retry_create; /* set on ACL-already-exists 0x0b */
    int create_retries;
    int purge_fail; /* Disconnect transport/errno fail — stop cleanly */
    unsigned pending_disc; /* handle we are waiting Disconnection Complete for */
    int pending_disc_done;
    int need_drop; /* set in on_event on 0x0b; connect loop performs drop */
    int cc_fail;   /* Connection Complete failure status (not 0x0b) */
    /* Incoming connections (btlink_accept): saved devices we accept. */
    int acc_n, acc_got;
    unsigned char acc_addr[8][6], acc_key[8][16], acc_kt[8];
    unsigned drop_hint; /* handle from Connection Complete 0x0b if any */
    int info_done;        /* Information Request exchange done on this ACL */
    unsigned char info_wait_id; /* ident of the INFO_REQ awaiting a reply */
    int info_got;         /* reply for info_wait_id arrived */
};

static int g_connect_fail;

/* Last successful ACL handle (this process). Used to drop stale links. */
static unsigned g_last_acl_handle;
static int g_close_confirmed = -1; /* -1 never closed, 0 unconfirmed, 1 ok */

static int same_addr(const unsigned char a[6], const unsigned char b[6])
{
    return memcmp(a, b, 6) == 0;
}

static int fire_cmd(hci_t hci, unsigned op, const void *params, int plen)
{
    if (!hci.ops || !hci.ops->cmd) return 0;
    return hci.ops->cmd(hci.ctx, op, params, plen);
}


static void pool_credit(acl_pool *p, int n, int from_event)
{
    acl_pool_credit(p, n, from_event, now_ms());
}

static void pool_fallback(acl_pool *p, long now)
{
    acl_pool_fallback(p, now);
}

static int tx_write(btlink *l, const unsigned char *pkt, int n)
{
    if (!l->hci.ops->acl_send(l->hci.ctx, pkt, n)) return 0;
    acl_pool_sent(&l->pool, now_ms());
    return 1;
}

/* Send queued ACL packets while credits allow. */
static void tx_flush(btlink *l)
{
    long now = now_ms();
    pool_fallback(&l->pool, now);
    while (l->txq_n > 0 && l->pool.outstanding < l->pool.limit) {
        txq_item *it = &l->txq[l->txq_head];
        if (it->media && it->due > now && l->txq_media <= PACE_TARGET_PKTS) break;   /* paced: not yet */
        if (!tx_write(l, it->d, it->len)) break;
        if (it->media) l->txq_media--;
        l->txq_head = (l->txq_head + 1) % TXQ_MAX;
        l->txq_n--;
    }
    if (now - l->pool.last_stats_ms >= 1000 && l->pool.sent) {
        acl_pool *p = &l->pool;
        log_line("acl: sent=%lu queued=%lu dropped=%lu (media %lu) credits evt=%lu fallback=%lu "
                 "outstanding=%d/%d q=%d media=%d maxq=%lu gap avg=%ldms max=%ldms "
                 "refill-after=%ldms stalls=%lu",
                 p->sent, p->queued, p->dropped, p->media_dropped, p->cred_evt, p->cred_fb,
                 p->outstanding, p->limit, l->txq_n, l->txq_media, p->max_q, p->gap_avg,
                 p->longest_gap, acl_pool_idle_limit(p), p->stalls);
        p->longest_gap = 0;
        p->last_stats_ms = now;
    }
}

static chan *chan_by_scid(btlink *l, unsigned scid)
{
    chan *c = l->ch, *end = l->ch + BTLINK_CHAN_MAX;
    for (; c < end; c++)
        if (c->st != CH_CLOSED && c->scid == scid) return c;
    return NULL;
}

static chan *chan_free_slot(btlink *l)
{
    chan *c = l->ch, *end = l->ch + BTLINK_CHAN_MAX;
    for (; c < end; c++)
        if (c->st == CH_CLOSED) return c;
    return NULL;
}

static unsigned char next_sig_id(btlink *l)
{
    if (++l->sig_id == 0) l->sig_id = 1;
    return l->sig_id;
}

/* Removes queue entry k (0 = oldest), keeping the order of the rest. */
static void txq_remove(btlink *l, int k)
{
    int i;
    if (l->txq[(l->txq_head + k) % TXQ_MAX].media) l->txq_media--;
    for (i = k; i > 0; i--)
        l->txq[(l->txq_head + i) % TXQ_MAX] = l->txq[(l->txq_head + i - 1) % TXQ_MAX];
    l->txq_head = (l->txq_head + 1) % TXQ_MAX;
    l->txq_n--;
    l->pool.dropped++;
}

/* Drops the oldest queued media packet (whole packet: the sink just sees an
 * RTP sequence gap, never a cut frame). 1 if one was dropped. */
static int txq_drop_oldest_media(btlink *l)
{
    int k;
    for (k = 0; k < l->txq_n; k++)
        if (l->txq[(l->txq_head + k) % TXQ_MAX].media) {
            txq_remove(l, k);
            l->pool.media_dropped++;
            return 1;
        }
    return 0;
}

/* One basic-mode L2CAP frame in one ACL packet (no fragmentation). */
static int l2_send_raw(btlink *l, unsigned cid, const unsigned char *d, int len)
{
    unsigned char pkt[HCI_PKT_MAX];
    const int frame = 4 + len;           /* L2CAP header + payload */

    if (!l->connected) return 0;
    if (frame + 4 > (int)sizeof pkt || (l->acl_mtu && frame > (int)l->acl_mtu)) {
        log_line("l2cap: %d-byte frame too large", len);
        return 0;
    }
    /* ACL header: handle | PB=10 (first, flushable); then L2CAP len, CID. */
    put16(pkt + 0, 0x2000u | (l->handle & 0x0FFFu));
    put16(pkt + 2, (unsigned)frame);
    put16(pkt + 4, (unsigned)len);
    put16(pkt + 6, cid);
    if (len > 0) memcpy(&pkt[8], d, (size_t)len);

    tx_flush(l);
    {
        long due = 0, now = now_ms();
        if (l->tx_is_media) {
            /* Pace media on a monotonic clock, one packet per its audio
             * length, so credits are not taken in bursts. A backlog (clock
             * in the past) is sent as fast as credits allow. */
            due = acl_pace_due(&l->media_clock, l->media_dur_ms, now, l->txq_media);
        }
        if (l->txq_n == 0 && l->pool.outstanding < l->pool.limit && due <= now)
            return tx_write(l, pkt, frame + 4);
        l->cur_due = due;
    }
    /* Queue (keep order). Media is capped per link and only beyond the cap
     * the oldest whole packet goes (RTP seq gap, timestamps continuous);
     * signalling is never dropped while there is media to drop. */
    if (l->tx_is_media && l->txq_media >= (l->media_cap > 0 ? l->media_cap : MEDIA_Q_DEFAULT))
        (void)txq_drop_oldest_media(l);
    if (l->txq_n == TXQ_MAX && !txq_drop_oldest_media(l)) txq_remove(l, 0);
    {
        txq_item *it = &l->txq[(l->txq_head + l->txq_n) % TXQ_MAX];
        it->media = (unsigned char)(l->tx_is_media != 0);
        it->due = it->media ? l->cur_due : 0;
        if (it->media) l->txq_media++;
        it->len = (uint16_t)(frame + 4);
        memcpy(it->d, pkt, (size_t)(frame + 4));
        l->txq_n++;
        l->pool.queued++;
        if ((unsigned long)l->txq_n > l->pool.max_q) l->pool.max_q = (unsigned long)l->txq_n;
    }
    return 1;
}

static int sig_send(btlink *l, unsigned char code, unsigned char id,
                    const unsigned char *d, int len)
{
    unsigned char cmd[64];

    if (len < 0 || len + 4 > (int)sizeof cmd) return 0;
    cmd[0] = code;
    cmd[1] = id;
    cmd[2] = (unsigned char)(len & 0xFF);
    cmd[3] = (unsigned char)(len >> 8);
    if (len > 0) memcpy(&cmd[4], d, (size_t)len);
    {
        char line[3 * 64 + 4];
        int i, pos = 0;
        for (i = 0; i < len + 4; i++)
            pos += snprintf(line + pos, sizeof line - (size_t)pos, "%02x%s",
                            cmd[i], i + 1 < len + 4 ? " " : "");
        line[pos] = 0;
        log_line("l2cap: sig tx len=%d: %s", len + 4, line);
    }
    return l2_send_raw(l, BTLINK_CID_SIGNALING, cmd, len + 4);
}

static void chan_set(chan *c, chan_state st)
{
    c->t_state = now_ms();
    c->st = st;
    if (st == CH_CONFIG) c->cfg_tries = 0;
    if (st != CH_OPEN) {
        c->cfg_ours_ok = 0;
        c->cfg_theirs_ok = 0;
    }
}

/* True when this outbound AVDTP channel is the *second* (media) on PSM 0x19. */
static int chan_is_avdtp_media(btlink *l, chan *c)
{
    int i;
    if (!c || c->psm != BTLINK_PSM_AVDTP || c->inbound) return 0;
    for (i = 0; i < BTLINK_CHAN_MAX; i++) {
        chan *o = &l->ch[i];
        if (o == c || o->st == CH_CLOSED) continue;
        /* Signalling may be ours or opened by the headset. */
        if (o->psm == BTLINK_PSM_AVDTP &&
            (o->st == CH_OPEN || o->st == CH_CONFIG || o->st == CH_CONNECTING))
            return 1;
    }
    return 0;
}

static void log_cfg_opts(const char *tag, unsigned scid, const unsigned char *opt, int optlen)
{
    char line[96];
    int i, pos = 0;
    if (optlen <= 0) {
        log_line("l2cap: %s scid %#x opts=(none)", tag, scid);
        return;
    }
    for (i = 0; i < optlen && pos < (int)sizeof line - 4; i++)
        pos += snprintf(line + pos, sizeof line - (size_t)pos, "%02x%s",
                        opt[i], (i + 1 < optlen) ? " " : "");
    log_line("l2cap: %s scid %#x opts(%d): %s", tag, scid, optlen, line);
}

static unsigned parse_peer_mtu(const unsigned char *opt, int optlen)
{
    int i = 0;
    while (i + 1 < optlen) {
        unsigned char typ = opt[i] & 0x7f;
        unsigned char olen = opt[i + 1];
        if (i + 2 + olen > optlen) break;
        if (typ == 0x01 && olen == 2)
            return (unsigned)opt[i + 2] | ((unsigned)opt[i + 3] << 8);
        i += 2 + olen;
    }
    return 0;
}

/* Build CFG_RSP: DestCID = remote endpoint (per spec). Echo MTU if known. */
static void chan_send_cfg_rsp(btlink *l, chan *ch, unsigned char id,
                              const unsigned char *opt, int optlen)
{
    unsigned char r[16] = { 0 };   /* flags = 0, result = success */
    int n = 6;
    unsigned mtu;
    put16(r, ch->dcid);
    mtu = ch->peer_mtu;
    if (!mtu && opt && optlen > 0)
        mtu = parse_peer_mtu(opt, optlen);
    if (mtu) {
        r[6] = 0x01; r[7] = 0x02;
        put16(r + 8, mtu);
        n = 10;
        ch->peer_mtu = mtu;
    }
    sig_send(l, L2SIG_CFG_RSP, id, r, n);
    if (mtu)
        log_line("l2cap: CFG_RSP id=%u scid %#x dcid %#x result=0 echo_mtu=%u",
                 id, ch->scid, ch->dcid, mtu);
    else
        log_line("l2cap: CFG_RSP id=%u scid %#x dcid %#x result=0",
                 id, ch->scid, ch->dcid);
}

/* CFG_REQ with an MTU option (672 by default, else the agreed value). */
static void chan_send_cfg(btlink *l, chan *c)
{
    unsigned char r[8];
    unsigned char id;
    int with_mtu = 1; /* always offer MTU 672 (or the peer's) */
    unsigned mtu = c->cfg_mtu ? c->cfg_mtu : (c->peer_mtu ? c->peer_mtu : 672);
    put16(r, c->dcid);
    put16(r + 2, 0); /* flags / continuation */
    id = next_sig_id(l);
    c->cfg_deferred = 0;
    if (with_mtu) {
        r[4] = 0x01; r[5] = 0x02; /* MTU */
        put16(r + 6, mtu);
        sig_send(l, L2SIG_CFG_REQ, id, r, 8);
        log_line("l2cap: CFG_REQ id=%u scid %#x dcid %#x (MTU %u) media=%d "
                 "ours=%d theirs=%d try=%d",
                 id, c->scid, c->dcid, mtu, chan_is_avdtp_media(l, c),
                 c->cfg_ours_ok, c->cfg_theirs_ok, c->cfg_tries + 1);
    } else {
        sig_send(l, L2SIG_CFG_REQ, id, r, 4);
        log_line("l2cap: CFG_REQ id=%u scid %#x dcid %#x (empty) media=%d "
                 "ours=%d theirs=%d try=%d",
                 id, c->scid, c->dcid, chan_is_avdtp_media(l, c),
                 c->cfg_ours_ok, c->cfg_theirs_ok, c->cfg_tries + 1);
    }
    c->t_cfg = now_ms();
    c->cfg_tries++;
}

/* Maybe send deferred CFG (media waited for peer CFG_REQ). */
static void chan_maybe_send_deferred_cfg(btlink *l, chan *c, long now)
{
    if (!c->cfg_deferred || c->st != CH_CONFIG || c->cfg_ours_ok) return;
    if (c->cfg_theirs_ok || now - c->t_cfg >= T_CFG_DEFER_MS) {
        log_line("l2cap: deferred CFG_REQ scid %#x (theirs=%d waited=%ld ms)",
                 c->scid, c->cfg_theirs_ok, now - c->t_cfg);
        chan_send_cfg(l, c);
    }
}

/* P2: flush queued CFG_RSP once dcid is known. */
static void chan_flush_pending_cfg(btlink *l, chan *ch)
{
    if (!ch->pending_cfg || !ch->dcid) return;
    log_line("l2cap: flushed pending CFG_RSP id=%u scid %#x dcid %#x optlen=%d",
             ch->pending_cfg_id, ch->scid, ch->dcid, ch->pending_cfg_optlen);
    if (ch->pending_cfg_optlen > 0)
        log_cfg_opts("pending CFG opts", ch->scid,
                     ch->pending_cfg_opt, ch->pending_cfg_optlen);
    {
        unsigned mtu = parse_peer_mtu(ch->pending_cfg_opt, ch->pending_cfg_optlen);
        if (mtu) ch->peer_mtu = mtu;
    }
    chan_send_cfg_rsp(l, ch, ch->pending_cfg_id,
                      ch->pending_cfg_opt, ch->pending_cfg_optlen);
    ch->pending_cfg = 0;
    ch->pending_cfg_optlen = 0;
    ch->cfg_theirs_ok = 1;
}

/* P3: send DISC_REQ before marking CLOSED so remote drops zombie half-config. */
static void chan_close_disc(btlink *l, chan *c)
{
    unsigned char r[4];
    if (!c || c->st == CH_CLOSED) return;
    if (c->dcid &&
        (c->st == CH_OPEN || c->st == CH_CONFIG || c->st == CH_CONNECTING)) {
        put16(r, c->dcid);
        put16(r + 2, c->scid);
        sig_send(l, L2SIG_DISC_REQ, next_sig_id(l), r, 4);
        log_line("l2cap: DISC_REQ PSM %#x scid %#x dcid %#x",
                 c->psm, c->scid, c->dcid);
    }
    c->pending_cfg = 0;
    c->pending_cfg_optlen = 0;
    c->cfg_deferred = 0;
    chan_set(c, CH_CLOSED);
}

static const char *psm_name(unsigned psm)
{
    switch (psm) {
    case BTLINK_PSM_SDP:      return "(SDP)";
    case 0x0003:              return "(RFCOMM)";
    case BTLINK_PSM_AVCTP:    return "(AVRCP control)";
    case BTLINK_PSM_AVDTP:    return "(AVDTP)";
    case BTLINK_PSM_AVCTP_BR: return "(AVRCP browsing)";
    default:                  return "";
    }
}

static void chan_check_open(btlink *l, chan *c)
{
    if (c->st == CH_CONFIG && c->cfg_ours_ok && c->cfg_theirs_ok) {
        chan_set(c, CH_OPEN);
        log_line("l2cap: channel PSM %#x open (scid %#x dcid %#x)",
                 c->psm, c->scid, c->dcid);
        if (c->psm == BTLINK_PSM_AVCTP) {
            unsigned char r[32];
            int n;
            l->avrcp_scid = c->scid;
            /* As controller: ask the headset for its volume + changes. */
            n = avrcp_build_register_volume(&l->avrcp, r, (int)sizeof r);
            if (n > 0) (void)l2_send_raw(l, c->dcid, r, n);
            log_line("avrcp: control channel up (scid %#x), registered for "
                     "VOLUME_CHANGED", c->scid);
        }
    }
}


/* Minimal SDP server replies so an inbound PSM 0x1 (headset querying us)
 * does not hang. Empty records — we are A2DP Source, not a Sink. */
static void sdp_reply_inbound(btlink *l, chan *c, const unsigned char *d, int len)
{
    unsigned char rsp[672];
    int max = (int)sizeof rsp, n;

    if (len < 5 || c->st != CH_OPEN) return;
    if (c->peer_mtu >= 48 && (int)c->peer_mtu < max) max = (int)c->peer_mtu;
    n = sdp_server_handle(d, len, rsp, max);
    if (n > 0 && l2_send_raw(l, c->dcid, rsp, n))
        log_line("sdp: request %#04x -> response %#04x (%d bytes)", d[0], rsp[0], n);
}

/* Hex log of an inbound signalling C-frame (CID 0x0001). */
static void log_sig_hex(const unsigned char *d, int len)
{
    char line[3 * 48 + 8];
    int off = 0;
    do {
        int i, pos = 0, n = len - off > 48 ? 48 : len - off;
        for (i = 0; i < n; i++)
            pos += snprintf(line + pos, sizeof line - (size_t)pos, "%02x%s",
                            d[off + i], i + 1 < n ? " " : "");
        line[pos] = 0;
        log_line("l2cap: sig rx len=%d [%d]: %s", len, off, line);
        off += n;
    } while (off < len);
}

static void sig_reject(btlink *l, unsigned char ident, unsigned reason,
                       const unsigned char *data, int dlen)
{
    unsigned char r[6];
    put16(r, reason);
    if (dlen > 4) dlen = 4;
    if (dlen > 0) memcpy(r + 2, data, (size_t)dlen);
    sig_send(l, L2SIG_REJECT, ident, r, 2 + (dlen > 0 ? dlen : 0));
}

static void on_signaling(btlink *l, const unsigned char *d, int len)
{
    const unsigned char *end = d + len;

    /* A C-frame may carry several commands back to back. */
    while (end - d >= 4) {
        const unsigned char op = d[0], ident = d[1];
        const int blen = (int)(d[2] | (d[3] << 8));
        const unsigned char *pl = &d[4];
        unsigned char out[16];
        chan *cc;

        if (blen > end - pl) break;

        switch (op) {
        case L2SIG_CONN_REQ:
            /* Accept inbound SDP / AVDTP (headsets often open SDP to us).
             * Reject other PSMs. */
            if (blen < 4) break;
            {
                unsigned psm = le16(pl);
                unsigned their_cid = le16(pl + 2);
                if (psm == BTLINK_PSM_SDP || psm == BTLINK_PSM_AVDTP ||
                    psm == BTLINK_PSM_AVCTP || psm == BTLINK_PSM_AVCTP_BR) {
                    chan *inc = chan_free_slot(l);
                    if (!inc) {
                        put16(out, 0);
                        put16(out + 2, their_cid);
                        put16(out + 4, 0x0004); /* no resources */
                        put16(out + 6, 0);
                        sig_send(l, L2SIG_CONN_RSP, ident, out, 8);
                        log_line("l2cap: inbound PSM %#x — no slot", psm);
                        break;
                    }
                    memset(inc, 0, sizeof *inc);
                    inc->psm = psm;
                    inc->inbound = 1;
                    inc->scid = l->next_scid++;
                    if (l->next_scid < 0x0040) l->next_scid = 0x0040;
                    inc->dcid = their_cid;
                    chan_set(inc, CH_CONFIG);
                    put16(out, inc->scid);
                    put16(out + 2, their_cid);
                    put16(out + 4, 0);
                    put16(out + 6, 0);
                    sig_send(l, L2SIG_CONN_RSP, ident, out, 8);
                    chan_send_cfg(l, inc);
                    log_line("l2cap: accepted inbound PSM %#x %s (scid %#x dcid %#x)",
                             psm, psm_name(psm), inc->scid, inc->dcid);
                } else {
                    put16(out, 0);
                    put16(out + 2, their_cid);
                    put16(out + 4, 0x0002); /* PSM not supported */
                    put16(out + 6, 0);
                    sig_send(l, L2SIG_CONN_RSP, ident, out, 8);
                    log_line("l2cap: rejected inbound PSM %#x %s", psm, psm_name(psm));
                }
            }
            break;

        case L2SIG_CONN_RSP:
            /* P1: pending (result=1) still carries valid CIDs — keep dcid. */
            if (blen < 8) break;
            cc = chan_by_scid(l, le16(pl + 2));
            if (!cc || cc->st != CH_CONNECTING) break;
            {
                unsigned result = le16(pl + 4);
                unsigned dcid = le16(pl);
                log_line("l2cap: CONN_RSP scid %#x dcid %#x result=%u",
                         cc->scid, dcid, result);
                if (result == 1) { /* pending */
                    if (dcid) cc->dcid = dcid;
                    if (cc->pending_cfg && cc->dcid)
                        chan_flush_pending_cfg(l, cc);
                    break;
                }
                if (result != 0) {
                    log_line("l2cap: PSM %#x refused (%u)", cc->psm, result);
                    cc->pending_cfg = 0;
                    chan_set(cc, CH_CLOSED);
                    break;
                }
                cc->dcid = dcid;
                chan_set(cc, CH_CONFIG);
                /* P2: answer any CFG_REQ that raced before CONN_RSP. */
                chan_flush_pending_cfg(l, cc);
                /* Media (2nd PSM 0x19): defer our CFG ~40ms so peer can go
                 * first — 0.1.4 succeeded with ~18ms natural gap; 0.1.6 raced. */
                if (chan_is_avdtp_media(l, cc) && !cc->cfg_theirs_ok) {
                    cc->cfg_deferred = 1;
                    cc->t_cfg = now_ms();
                    log_line("l2cap: deferring CFG_REQ for media scid %#x "
                             "(%d ms)", cc->scid, T_CFG_DEFER_MS);
                } else {
                    chan_send_cfg(l, cc);
                }
                chan_check_open(l, cc);
            }
            break;

        case L2SIG_CFG_REQ:
            /* P2: never ignore — queue if no dcid yet; else CFG_RSP now. */
            if (blen < 4) break;
            cc = chan_by_scid(l, le16(pl));
            if (!cc) {
                log_line("l2cap: CFG_REQ for unknown cid %#x — reject", le16(pl));
                {
                    unsigned char cid2[4];
                    put16(cid2, le16(pl));      /* local CID (as sent) */
                    put16(cid2 + 2, 0);
                    sig_reject(l, ident, 0x0002, cid2, 4); /* invalid CID */
                }
                break;
            }
            {
                int optlen = blen > 4 ? blen - 4 : 0;
                const unsigned char *opt = pl + 4;
                unsigned flags = le16(pl + 2);
                unsigned mtu;
                log_line("l2cap: CFG_REQ ident=%u scid %#x dcid %#x flags=%u "
                         "optlen=%d st=%d media=%d ours=%d theirs=%d",
                         ident, cc->scid, cc->dcid, flags, optlen, (int)cc->st,
                         chan_is_avdtp_media(l, cc),
                         cc->cfg_ours_ok, cc->cfg_theirs_ok);
                if (optlen > 0)
                    log_cfg_opts("CFG_REQ", cc->scid, opt, optlen);
                mtu = parse_peer_mtu(opt, optlen);
                if (mtu) {
                    cc->peer_mtu = mtu;
                    log_line("l2cap: peer MTU %u on scid %#x", mtu, cc->scid);
                }
                if (cc->st == CH_CONNECTING && !cc->dcid) {
                    cc->pending_cfg = 1;
                    cc->pending_cfg_id = ident;
                    cc->pending_cfg_optlen = optlen > CFG_OPT_MAX ? CFG_OPT_MAX : optlen;
                    if (cc->pending_cfg_optlen > 0)
                        memcpy(cc->pending_cfg_opt, opt, (size_t)cc->pending_cfg_optlen);
                    log_line("l2cap: CFG_REQ queued (no dcid yet) ident=%u scid %#x "
                             "optlen=%d", ident, cc->scid, cc->pending_cfg_optlen);
                    break;
                }
                /* Accepted in any state once we know their CID (before or
                 * after our CFG_REQ, with or right after CONN_RSP). */
                if (cc->st == CH_CONNECTING) {
                    int ours_was = cc->cfg_ours_ok;
                    chan_set(cc, CH_CONFIG);
                    cc->cfg_ours_ok = ours_was;
                }
                if (flags & 0x0001) {
                    /* Continuation: ack this part (flags=1), wait for more. */
                    unsigned char r[6];
                    put16(r, cc->dcid);
                    put16(r + 2, 0x0001);
                    put16(r + 4, 0);
                    sig_send(l, L2SIG_CFG_RSP, ident, r, 6);
                    log_line("l2cap: CFG_REQ continuation acked scid %#x", cc->scid);
                    break;
                }
                /* Accept their config (Basic). Echo MTU when present. */
                chan_send_cfg_rsp(l, cc, ident, opt, optlen);
                if (!cc->cfg_ours_ok && cc->cfg_tries == 0) {
                    chan_send_cfg(l, cc);
                } else if (cc->cfg_deferred && !cc->cfg_ours_ok) {
                    /* Peer went first on media — send our CFG now. */
                    chan_send_cfg(l, cc);
                }
                cc->cfg_theirs_ok = 1;
                chan_check_open(l, cc);
            }
            break;

        case L2SIG_CFG_RSP:
            if (blen < 6) break;
            cc = chan_by_scid(l, le16(pl));
            if (!cc) {
                log_line("l2cap: CFG_RSP for unknown cid %#x", le16(pl));
                break;
            }
            {
                unsigned result = le16(pl + 4);
                int optlen = blen > 6 ? blen - 6 : 0;
                log_line("l2cap: CFG_RSP scid %#x dcid %#x result=%u optlen=%d "
                         "media=%d ours=%d theirs=%d",
                         cc->scid, cc->dcid, result, optlen,
                         chan_is_avdtp_media(l, cc),
                         cc->cfg_ours_ok, cc->cfg_theirs_ok);
                if (optlen > 0)
                    log_cfg_opts("CFG_RSP", cc->scid, pl + 6, optlen);
                if (result == 0) {
                    cc->cfg_ours_ok = 1;
                    chan_check_open(l, cc);
                } else {
                    log_line("l2cap: config result %u on PSM %#x — retry MTU",
                             result, cc->psm);
                    cc->cfg_want_mtu = 1;
                    if (cc->peer_mtu) cc->cfg_mtu = cc->peer_mtu;
                    cc->cfg_ours_ok = 0;
                    if (cc->st == CH_CONFIG && cc->cfg_tries < CFG_TRIES)
                        chan_send_cfg(l, cc);
                }
            }
            break;

        case L2SIG_DISC_REQ:
            if (blen < 4) break;
            sig_send(l, L2SIG_DISC_RSP, ident, pl, 4);
            cc = chan_by_scid(l, le16(pl));
            if (cc) chan_set(cc, CH_CLOSED);
            break;

        case L2SIG_INFO_RSP:
            if (blen >= 4)
                log_line("l2cap: INFO_RSP id=%u type %u result %u", ident,
                         le16(pl), le16(pl + 2));
            if (ident == l->info_wait_id) l->info_got = 1;
            break;

        case L2SIG_DISC_RSP:
        case L2SIG_ECHO_RSP:
            break;

        case L2SIG_ECHO_REQ:
            sig_send(l, L2SIG_ECHO_RSP, ident, pl, blen < 32 ? blen : 32);
            break;

        case L2SIG_INFO_REQ:
            /* Type 2: extended features = none (basic mode only).
             * Type 3: fixed channels = signalling (bit 1) only. */
            if (blen < 2) break;
            log_line("l2cap: INFO_REQ type %u id=%u — answering", le16(pl), ident);
            memset(out, 0, sizeof out);
            put16(out, le16(pl));
            if (le16(pl) == 2) {
                sig_send(l, L2SIG_INFO_RSP, ident, out, 8);
            } else if (le16(pl) == 3) {
                out[4] = 0x02;
                sig_send(l, L2SIG_INFO_RSP, ident, out, 12);
            } else {
                put16(out + 2, 1);
                sig_send(l, L2SIG_INFO_RSP, ident, out, 4);
            }
            break;

        case L2SIG_REJECT:
            log_line("l2cap: command rejected (reason %u)",
                     blen >= 2 ? le16(pl) : 0);
            break;

        default:
            /* Command not understood (Core Vol 3 Part A 4.1). Responses
             * (even codes we do not track) are not rejected. */
            log_line("l2cap: unknown signal %#04x id=%u len=%d — Command Reject",
                     op, ident, blen);
            sig_reject(l, ident, 0x0000, NULL, 0);
            break;
        }
        d = pl + blen;
    }
}

static void avrcp_send(btlink *l, const unsigned char *p, int n)
{
    chan *c = l->avrcp_scid ? chan_by_scid(l, l->avrcp_scid) : NULL;
    if (c && c->st == CH_OPEN && n > 0) (void)l2_send_raw(l, c->dcid, p, n);
}

/* AVRCP control channel: absolute volume both ways (see avrcp.c). */
static void avctp_reply(btlink *l, chan *c, const unsigned char *d, int len)
{
    unsigned char r[128];
    int n;

    if (c->psm == BTLINK_PSM_AVCTP_BR) {
        /* Browsing: no media player, General Reject every PDU. */
        if (len < 4 || (d[0] & 0x02)) return;
        r[0] = (unsigned char)((d[0] & 0xF0) | 0x02);
        r[1] = d[1]; r[2] = d[2];
        r[3] = 0xA0; r[4] = 0x00; r[5] = 0x01; r[6] = 0x00;
        (void)l2_send_raw(l, c->dcid, r, 7);
        log_line("avrcp: browsing PDU %#x -> General Reject", d[3]);
        return;
    }
    l->avrcp_scid = c->scid;
    n = avrcp_input(&l->avrcp, d, len, r, (int)sizeof r);
    if (n > 0) (void)l2_send_raw(l, c->dcid, r, n);
    if (l->avrcp.need_register) {
        l->avrcp.need_register = 0;
        n = avrcp_build_register_volume(&l->avrcp, r, (int)sizeof r);
        avrcp_send(l, r, n);
    }
}

static void on_frame(btlink *l, unsigned cid, const unsigned char *d, int len)
{
    chan *c;

    if (cid == BTLINK_CID_SIGNALING) {
        log_sig_hex(d, len);
        on_signaling(l, d, len);
        return;
    }
    c = chan_by_scid(l, cid);
    if (!c || c->st != CH_OPEN) return;
    /* Inbound SDP: headset is the client — answer so their stack settles.
     * Never treat this channel as our SDP client (see sdp_a2dp.c). */
    if (c->inbound && c->psm == BTLINK_PSM_SDP) {
        sdp_reply_inbound(l, c, d, len);
        return;
    }
    if (c->psm == BTLINK_PSM_AVCTP || c->psm == BTLINK_PSM_AVCTP_BR) {
        avctp_reply(l, c, d, len);
        return;
    }
    if (c->inbound && !c->rx && l->in_rx && c->psm == l->in_rx_psm) {
        l->in_rx(l->in_rx_ud, c->scid, d, len);
        return;
    }
    if (c->inbound && c->psm == BTLINK_PSM_AVDTP && !c->rx && !c->wait_armed) {
        if (l->n_early < 6 && len <= 160) {
            l->early[l->n_early].scid = c->scid;
            l->early[l->n_early].len = len;
            memcpy(l->early[l->n_early].d, d, (size_t)len);
            l->n_early++;
            log_line("avdtp: headset sent %d bytes (hdr %02x %02x) before setup — kept",
                     len, d[0], len > 1 ? d[1] : 0);
        }
        return;
    }
    if (c->wait_armed && c->wait_buf && len <= c->wait_max) {
        memcpy(c->wait_buf, d, (size_t)len);
        c->wait_len = len;
        c->wait_armed = 0;
    }
    if (c->rx) c->rx(c->rx_ud, c->scid, d, len);
}

/* ACL inbound path. Each HCI ACL fragment = 4-byte header
 * (12-bit handle | PB<<12 | BC<<14, then 16-bit payload length).
 * PB 0b10 opens a fresh L2CAP PDU, PB 0b01 extends the pending one.
 * When the gathered bytes cover the L2CAP basic header length + 4,
 * the PDU is handed to on_frame(). */
#define ACL_PB_CONTINUE 0x1
#define ACL_PB_START    0x2

static void acl_reset(btlink *l)
{
    l->rx_len = 0;
    l->rx_need = 0;
}

/* Deliver once the L2CAP basic header (len, cid) is complete and the
 * whole PDU has arrived. Extra bytes past one PDU are discarded. */
static void acl_try_deliver(btlink *l)
{
    int pdu;
    if (l->rx_len < 4) return;               /* header still split */
    if (l->rx_need == 0) {
        l->rx_need = (int)le16(l->rx) + 4;
        if (l->rx_need > (int)sizeof l->rx) {
            log_line("l2cap: PDU %d bytes too large, dropped", l->rx_need);
            acl_reset(l);
            return;
        }
    }
    if (l->rx_len < l->rx_need) return;
    pdu = l->rx_need;
    acl_reset(l);
    on_frame(l, le16(l->rx + 2), l->rx + 4, pdu - 4);
}

static void on_acl(btlink *l, const unsigned char *pkt, int len)
{
    unsigned word, handle, flags;
    int frag;
    const unsigned char *body = pkt + 4;

    if (!l || len < 4) return;
    word   = le16(pkt);
    handle = word & 0x0FFF;
    flags  = (word >> 12) & 0x3;
    frag   = (int)le16(pkt + 2);
    if (handle != (l->handle & 0x0FFF)) return;   /* other links: not ours */
    if (frag > len - 4) {
        log_line("l2cap: short ACL (hdr %d, got %d) — truncating", frag, len - 4);
        frag = len - 4;
    }

    if (flags == ACL_PB_CONTINUE) {
        if (l->rx_len == 0) {
            log_line("l2cap: ACL continuation without start (%d bytes) dropped", frag);
            return;
        }
    } else {
        if (l->rx_len)
            log_line("l2cap: new ACL start, %d-byte partial PDU discarded", l->rx_len);
        acl_reset(l);
    }
    if (frag > (int)sizeof l->rx - l->rx_len) {
        log_line("l2cap: ACL reassembly overflow, dropped");
        acl_reset(l);
        return;
    }
    memcpy(&l->rx[l->rx_len], body, (size_t)frag);
    l->rx_len += frag;
    acl_try_deliver(l);
}

/* Disconnect one handle; wait briefly for Disconnection Complete.
 * Returns 1 if Disc Complete seen, 0 if timed out / unknown, -1 on
 * transport fail (e.g. errno 5) — caller must STOP (no infinite loop). */
static int disconnect_handle(btlink *l, unsigned h, int wait_ms)
{
    unsigned char p[3];
    long deadline;

    if (!l || !h || h > 0x0EFF) return 0;
    put16(p, h);
    p[2] = 0x13; /* Remote User Terminated Connection */
    log_line("btlink: HCI Disconnect handle %#05x", h);
    l->pending_disc = h;
    l->pending_disc_done = 0;
    if (!fire_cmd(l->hci, HB_OP_DISCONNECT, p, 3)) {
        log_line("btlink: Disconnect %#05x transport fail (errno) — abort", h);
        l->pending_disc = 0;
        l->purge_fail = 1;
        return -1;
    }
    deadline = now_ms() + (wait_ms > 0 ? wait_ms : 1500);
    while (now_ms() < deadline) {
        if (btlink_pump(l, 40) < 0) {
            l->pending_disc = 0;
            l->purge_fail = 1;
            return -1;
        }
        if (l->pending_disc_done) {
            log_line("btlink: Disconnection Complete %#05x", h);
            l->pending_disc = 0;
            return 1;
        }
    }
    log_line("btlink: Disconnect %#05x — no Disc Complete (unknown ok)", h);
    l->pending_disc = 0;
    return 0;
}

static void cand_add(unsigned *cands, int *n, int max, unsigned h)
{
    int j;
    if (!h || h > 0x0EFF) return;
    for (j = 0; j < *n; j++) if (cands[j] == h) return;
    if (*n < max) cands[(*n)++] = h;
}

/* Drop ACL handles that THIS app created (Connection Complete for our
 * target). Handles owned by the system stack (DualSense etc.) are never
 * touched, so no guessed handle (e.g. 0x001) is ever disconnected.
 * Returns 0 ok, -1 transport fail (purge_fail set). */
static int drop_known_handles(btlink *l, unsigned hint)
{
    unsigned cands[2];
    int n = 0, i, rc, got = 0;

    (void)hint; /* handle field of a failed Connection Complete is not ours */
    if (l->connected) cand_add(cands, &n, 2, l->handle);
    if (g_last_acl_handle && g_last_acl_handle != l->handle)
        cand_add(cands, &n, 2, g_last_acl_handle);

    if (n == 0) {
        log_line("btlink: no ACL owned by this app — nothing to drop");
        return 0;
    }
    log_line("btlink: dropping %d ACL handle(s) owned by this app", n);
    for (i = 0; i < n; i++) {
        rc = disconnect_handle(l, cands[i], 1500);
        if (rc < 0) return -1;
        if (rc > 0) got = 1;
        if (cands[i] == g_last_acl_handle) g_last_acl_handle = 0;
    }
    if (got)
        log_line("btlink: own ACL dropped");
    return 0;
}

/* Public: best-effort drop before CREATE (connect start). */
int btlink_drop_stale(btlink *l, const unsigned char addr[6])
{
    long w;
    if (!l) return 0;
    if (addr) memcpy(l->addr, addr, 6);
    l->purge_fail = 0;
    log_line("btlink: checking for an ACL left by this app");
    if (drop_known_handles(l, 0) < 0) {
        log_line("btlink: preemptive drop aborted (transport)");
        return 0;
    }
    w = now_ms() + 400;
    while (now_ms() < w) {
        if (btlink_pump(l, 40) < 0) break;
    }
    return 1;
}

static void on_event(btlink *l, const unsigned char *ev, int nEv)
{

    if (nEv < 1) return;

    if (ev[0] == 0x0F && nEv >= 6) {
        unsigned rop = (unsigned)ev[4] | ((unsigned)ev[5] << 8);
        if (rop == HB_OP_CREATE_CONNECTION) {
            if (ev[2] == 0) {
                log_line("btlink: CREATE_CONNECTION accepted");
            } else if (ev[2] == 0x0B) {
                log_line("btlink: ACL already exists (cmd-status) — schedule drop + retry");
                l->need_drop = 1;
                l->drop_hint = 0;
                l->retry_create = 1;
            } else {
                log_line("btlink: CREATE_CONNECTION status %#04x", ev[2]);
            }
        } else if (rop == HB_OP_AUTH_REQUESTED && ev[2] != 0) {
            log_line("btlink: AUTH cmd-status %#04x — will retry", ev[2]);
            l->auth_sent = 0;
            l->t_conn = now_ms();
        }
        return;
    }

    if (ev[0] == 0x04 && nEv >= 12 && l->acc_n && !l->connected && !l->acc_got &&
        ev[11] == 0x01) {                 /* Connection Request, ACL */
        int k;
        for (k = 0; k < l->acc_n; k++) {
            if (!same_addr(ev + 2, l->acc_addr[k])) continue;
            {
                unsigned char ap[7];
                char astr[18];
                memcpy(l->addr, ev + 2, 6);
                memcpy(l->link_key, l->acc_key[k], 16);
                l->key_type = l->acc_kt[k];
                l->have_key = 1;
                l->fresh_pair = 0;
                l->acc_got = k + 1;
                memcpy(ap, ev + 2, 6);
                ap[6] = 0x00;             /* become central: we stream */
                fire_cmd(l->hci, 0x0409, ap, 7);   /* Accept Connection Request */
                hci_addr_str(ev + 2, astr);
                log_line("btlink: incoming connection from saved %s — accepting", astr);
            }
            break;
        }
        return;
    }

    if (ev[0] == 0x03 && nEv >= 13 && same_addr(ev + 5, l->addr)) {
        if (ev[2] != 0) {
            log_line("btlink: Connection Complete fail %#04x", ev[2]);
            l->connected = 0;
            /* 0x0b ACL already exists: disconnect known handles for this
             * BD_ADDR, wait Disc Complete, then retry CREATE once. */
            if (ev[2] == 0x0B) {
                unsigned h = le16(ev + 3) & 0x0FFF;
                log_line("btlink: 0x0b — schedule drop (event handle %#05x)", h);
                l->need_drop = 1;
                l->drop_hint = h;
                l->retry_create = 1;
            } else {
                l->cc_fail = ev[2];
            }
            return;
        }
        l->handle = le16(ev + 3) & 0x0FFF;
        l->connected = 1;
        l->t_conn = now_ms();
        g_last_acl_handle = l->handle;
        log_line("btlink: ACL up handle %#05x", l->handle);
        if (btlink_on_acl_up) btlink_on_acl_up(l->addr);
        {
            /* Learn the peer's clock offset now, for a fast reconnect page. */
            unsigned char co[2];
            put16(co, l->handle);
            (void)fire_cmd(l->hci, 0x041F, co, 2);
        }
        if (!l->name[0] || !strcmp(l->name, "-")) {
            /* No name known: ask the device (Remote Name Request). */
            unsigned char rn[10];
            memcpy(rn, l->addr, 6);
            rn[6] = 0x01; rn[7] = 0; rn[8] = 0; rn[9] = 0;
            (void)fire_cmd(l->hci, 0x0419, rn, 10);
            log_line("btlink: Remote Name Request");
        }
        {
            /* Streaming wants the link active: allow role switch only (no
             * hold / sniff / park) on our handle. */
            unsigned char lp[4];
            put16(lp, l->handle);
            put16(lp + 2, 0x0001);
            fire_cmd(l->hci, 0x080D, lp, 4);
            /* Notice a headset that went away (back in its case) within
             * ~2 s instead of the 20 s default supervision timeout. */
            put16(lp + 2, 3200);              /* 3200 slots x 0.625 ms */
            fire_cmd(l->hci, 0x0C37, lp, 4);
        }
        return;
    }

    if (ev[0] == 0x05 && nEv >= 6) {
        unsigned dh = le16(ev + 3) & 0x0FFF;
        if (l->pending_disc && dh == (l->pending_disc & 0x0FFF)) {
            l->pending_disc_done = 1;
            log_line("btlink: disconnected (reason %#04x) pending %#05x",
                     ev[5], dh);
        }
        if (l->connected && dh == (l->handle & 0x0FFF)) {
            log_line("btlink: disconnected (reason %#04x)", ev[5]);
            l->connected = 0;
            if (g_last_acl_handle == dh) g_last_acl_handle = 0;
        }
        return;
    }

    if (ev[0] == 0x06 && nEv >= 5 && l->connected &&
        (le16(ev + 3) & 0x0FFF) == (l->handle & 0x0FFF)) {
        if (ev[2] == 0) {
            l->auth_ok = 1;
            log_line("btlink: Authentication Complete OK");
        } else {
            log_line("btlink: Authentication Complete fail %#04x", ev[2]);
        }
        return;
    }

    if (ev[0] == 0x08 && nEv >= 6 && l->connected &&
        (le16(ev + 3) & 0x0FFF) == (l->handle & 0x0FFF)) {
        if (!ev[2] && ev[5] != 0) {
            l->auth_ok = l->enc_on = 1;
            log_line("btlink: Encryption Change enabled");
        }
        return;
    }

    if (ev[0] == 0x14 && nEv >= 8 &&    /* Mode Change */
        (le16(ev + 3) & 0x0FFF) == (l->handle & 0x0FFF)) {
        static const char *mn[] = { "active", "hold", "sniff", "park" };
        unsigned mode = ev[5], iv = le16(ev + 6);
        log_line("btlink: mode change -> %s (status %#04x, interval %u slots = %u ms)",
                 mode < 4 ? mn[mode] : "?", ev[2], iv, iv * 625 / 1000);
        if (!ev[2] && mode == 2) {
            unsigned char h[2];
            put16(h, l->handle);
            l->sniff_seen++;
            fire_cmd(l->hci, 0x0804, h, 2);         /* Exit Sniff Mode */
            log_line("btlink: headset went to sniff while streaming — asking for active mode");
        }
        return;
    }

    if (ev[0] == 0x13 && nEv >= 3) {   /* Number Of Completed Packets */
        const unsigned char *e = ev + 3, *stop = ev + nEv;
        int left = ev[2];
        for (; left > 0 && e + 4 <= stop; left--, e += 4) {
            if ((le16(e) & 0x0FFF) != (l->handle & 0x0FFF)) continue;
            pool_credit(&l->pool, (int)le16(e + 2), 1);
        }
        return;
    }

    if (ev[0] == 0x16 && nEv >= 8 && same_addr(ev + 2, l->addr)) {
        unsigned char pin[23] = { 0 };
        int q;
        for (q = 0; q < 6; q++) pin[q] = ev[2 + q];
        pin[6] = 4;                        /* legacy PIN "0000" */
        pin[7] = pin[8] = pin[9] = pin[10] = '0';
        fire_cmd(l->hci, HB_OP_PIN_CODE_REPLY, pin, (int)sizeof pin);
        log_line("btlink: PIN Code Reply 0000");
        return;
    }

    if (ev[0] == 0x17 && nEv >= 8 && same_addr(ev + 2, l->addr)) {
        unsigned char kr[22];
        int q;
        if (!l->have_key) {
            fire_cmd(l->hci, HB_OP_LINK_KEY_NEG_REPLY, ev + 2, 6);
            log_line("btlink: Link Key Neg Reply");
            return;
        }
        for (q = 0; q < 6; q++) kr[q] = ev[2 + q];
        for (q = 0; q < 16; q++) kr[6 + q] = l->link_key[q];
        fire_cmd(l->hci, HB_OP_LINK_KEY_REPLY, kr, (int)sizeof kr);
        log_line("btlink: Link Key Reply (stored)");
        return;
    }

    if (ev[0] == 0x18 && nEv >= 25 && same_addr(ev + 2, l->addr)) {
        l->have_key = 1;
        l->key_type = ev[24];
        memcpy(l->link_key, &ev[8], sizeof l->link_key);
        log_line("btlink: Link Key Notification type=%u", l->key_type);
        return;
    }

    if (ev[0] == 0x31 && nEv >= 8 && same_addr(ev + 2, l->addr)) {
        /* NoInputNoOutput, no OOB, General Bonding without MITM. */
        unsigned char io[9] = { 0, 0, 0, 0, 0, 0, 0x03, 0x00, 0x04 };
        int q;
        for (q = 0; q < 6; q++) io[q] = ev[2 + q];
        fire_cmd(l->hci, HB_OP_IO_CAP_REPLY, io, (int)sizeof io);
        log_line("btlink: IO Cap Reply (NoInputNoOutput)");
        return;
    }

    if (ev[0] == 0x07 && nEv >= 9 && ev[2] == 0 && same_addr(ev + 3, l->addr)) {
        int k, m = nEv - 9;                      /* Remote Name Request Complete */
        if (m > (int)sizeof l->name - 1) m = (int)sizeof l->name - 1;
        for (k = 0; k < m && ev[9 + k]; k++) l->name[k] = (char)ev[9 + k];
        l->name[k] = 0;
        if (k) log_line("btlink: remote name \"%s\"", l->name);
        return;
    }

    if (ev[0] == 0x33 && nEv >= 8 && same_addr(ev + 2, l->addr)) {
        fire_cmd(l->hci, HB_OP_USER_CONFIRM_REPLY, ev + 2, 6);
        log_line("btlink: User Confirmation Reply");
        return;
    }
}

static void drive_auth(btlink *l)
{
    unsigned char p[4];
    long now = now_ms();

    if (!l->connected) return;
    /* Reconnect: give the remote a short window to auth itself. */
    if (!l->auth_ok && !l->auth_sent &&
        now - l->t_conn >= (l->fresh_pair ? 0 : 800)) {
        put16(p, l->handle);
        fire_cmd(l->hci, HB_OP_AUTH_REQUESTED, p, 2);
        l->auth_sent = 1;
        log_line("btlink: Authentication Requested");
    }
    if (l->auth_ok && !l->enc_on && !l->enc_sent) {
        put16(p, l->handle);
        p[2] = 1;
        fire_cmd(l->hci, HB_OP_SET_ENCRYPTION, p, 3);
        l->enc_sent = 1;
        log_line("btlink: Set Connection Encryption");
    }
}

static void chan_tick(btlink *l, chan *c, long now)
{
    long cfg_lim = chan_is_avdtp_media(l, c) ? T_CFG_TIMEOUT_MEDIA :
                   (l->cfg_timeout_ms > 0 ? l->cfg_timeout_ms : T_CFG_TIMEOUT);

    if (c->st == CH_CONNECTING && now - c->t_state > T_CHAN_CONNECT) {
        log_line("l2cap: PSM %#x connect timeout", c->psm);
        chan_close_disc(l, c); /* P3: DISC if dcid known (pending path) */
    } else if (c->st == CH_CONFIG && c->cfg_deferred) {
        chan_maybe_send_deferred_cfg(l, c, now);
    } else if (c->st == CH_CONFIG && now - c->t_state > cfg_lim) {
        log_line("l2cap: PSM %#x config stuck (ours=%d theirs=%d tries=%d "
                 "media=%d scid %#x dcid %#x peer_mtu=%u)",
                 c->psm, c->cfg_ours_ok, c->cfg_theirs_ok, c->cfg_tries,
                 chan_is_avdtp_media(l, c), c->scid, c->dcid, c->peer_mtu);
        chan_close_disc(l, c); /* P3: DISC before CLOSED — no zombie half-config */
    } else if (c->st == CH_CONFIG && c->cfg_ours_ok && !c->cfg_theirs_ok &&
               !c->cfg_nudged && now - c->t_cfg >= 2000) {
        /* Peer accepted ours but has not configured its side: offer our
         * config once more (a fresh ident) so it can respond with its own. */
        c->cfg_nudged = 1;
        c->cfg_ours_ok = 0;
        log_line("l2cap: no peer CFG_REQ 2 s after CFG_RSP — re-sending ours (scid %#x)",
                 c->scid);
        chan_send_cfg(l, c);
    } else if (c->st == CH_CONFIG && !c->cfg_ours_ok && !c->cfg_deferred &&
               now - c->t_cfg >= T_CFG_RESEND) {
        if (c->cfg_tries >= CFG_TRIES) {
            log_line("l2cap: PSM %#x config unanswered (scid %#x)", c->psm, c->scid);
            chan_close_disc(l, c);
        } else {
            /* Empty first; after 2 unanswered tries escalate to MTU (media especially). */
            if (c->cfg_tries >= 2 && !c->cfg_want_mtu) {
                c->cfg_want_mtu = 1;
                if (c->peer_mtu) c->cfg_mtu = c->peer_mtu;
                log_line("l2cap: escalate CFG to MTU on scid %#x (peer_mtu=%u)",
                         c->scid, c->peer_mtu);
            }
            chan_send_cfg(l, c);
        }
    }
}

btlink *btlink_create(hci_t hci, int acl_mtu, int acl_buffers)
{
    btlink *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    l->hci = hci;
    l->acl_mtu = acl_mtu > 0 ? (unsigned)acl_mtu : 1021;
    l->pool.limit = (acl_buffers > 0 ? acl_buffers : 7) - ACL_RESERVE;
    if (l->pool.limit < 1) l->pool.limit = 1;
    l->next_scid = 0x0040;
    avrcp_init(&l->avrcp, 64);
    return l;
}

void btlink_destroy(btlink *l)
{
    if (!l) return;
    btlink_disconnect(l); /* no-op unless we own a live ACL */
    free(l);
}

int btlink_is_up(const btlink *l)
{
    return l && l->connected && l->enc_on;
}

unsigned btlink_handle(const btlink *l)
{
    return l ? l->handle : 0;
}

const unsigned char *btlink_addr(const btlink *l)
{
    return l ? l->addr : NULL;
}

int btlink_pump(btlink *l, int timeout_ms)
{
    unsigned char buf[HCI_PKT_MAX];
    int n, i;
    long now;

    if (!l || !l->hci.ops) return -1;
    if (l->hci.ops->pump(l->hci.ctx, timeout_ms) < 0) {
        l->dead = 1;
        return -1;
    }
    while ((n = l->hci.ops->next_event(l->hci.ctx, buf, (int)sizeof buf)) > 0)
        on_event(l, buf, n);
    while ((n = l->hci.ops->next_acl(l->hci.ctx, buf, (int)sizeof buf)) > 0)
        on_acl(l, buf, n);

    tx_flush(l);
    drive_auth(l);
    now = now_ms();
    for (i = 0; i < BTLINK_CHAN_MAX; i++) {
        if (l->ch[i].st != CH_CLOSED)
            chan_tick(l, &l->ch[i], now);
    }
    return l->dead ? -1 : 0;
}

int (*btlink_abort_connect)(const unsigned char addr[6]);
void (*btlink_on_acl_up)(const unsigned char addr[6]);

static void link_reset(btlink *l)
{
    l->connected = l->auth_sent = l->auth_ok = l->enc_sent = l->enc_on = 0;
    l->handle = 0;
    l->retry_create = l->create_retries = l->purge_fail = 0;
    l->need_drop = 0;
    l->cc_fail = 0;
    l->drop_hint = 0;
    l->pending_disc = 0;
    l->pending_disc_done = 0;
    l->info_done = 0;
    l->txq_n = l->txq_head = l->txq_media = 0;
    l->pool.outstanding = 0;
    l->acc_n = l->acc_got = 0;
}

int btlink_adopt(btlink *l, unsigned handle, const unsigned char addr[6],
                 const unsigned char link_key[16], unsigned char key_type,
                 const char *name)
{
    unsigned char lp[4];
    int i;
    if (!l || !addr || !handle) return 0;
    link_reset(l);
    /* Fresh L2CAP state: no channels, fresh ids/cids, info exchange redone. */
    for (i = 0; i < BTLINK_CHAN_MAX; i++) memset(&l->ch[i], 0, sizeof l->ch[i]);
    l->next_scid = 0x0040;
    memcpy(l->addr, addr, 6);
    if (link_key) {
        memcpy(l->link_key, link_key, 16);
        l->key_type = key_type;
        l->have_key = 1;
    }
    l->fresh_pair = 0;
    snprintf(l->name, sizeof l->name, "%s", name ? name : "");
    l->handle = handle & 0x0FFF;
    l->connected = l->auth_ok = l->enc_on = 1;
    l->auth_sent = l->enc_sent = 1;
    l->t_conn = now_ms();
    g_last_acl_handle = l->handle;
    log_line("btlink: took over the pairing ACL %#05x (encrypted)", l->handle);
    put16(lp, l->handle);
    put16(lp + 2, 0x0001);                 /* role switch only, no sniff */
    fire_cmd(l->hci, 0x080D, lp, 4);
    return 1;
}

int btlink_accept(btlink *l, const unsigned char (*addrs)[6],
                  const unsigned char (*keys)[16], const unsigned char *key_types,
                  int n, int timeout_ms, int *which)
{
    unsigned char se = 0, cc[8];
    int cc_len = 0, old = -1, ok = 0, k;
    long deadline;

    if (!l || n <= 0) return 0;
    if (n > 8) n = 8;
    link_reset(l);
    memset(l->addr, 0, 6);
    l->acc_n = n;
    for (k = 0; k < n; k++) {
        memcpy(l->acc_addr[k], addrs[k], 6);
        memcpy(l->acc_key[k], keys[k], 16);
        l->acc_kt[k] = key_types[k];
    }
    if (hci_cmd_sync(l->hci, HB_OP_READ_SCAN_ENABLE, NULL, 0, cc, &cc_len, (int)sizeof cc) &&
        cc_len >= 7)
        old = cc[6];
    if (old < 0 || !(old & 0x02)) {
        se = (unsigned char)((old < 0 ? 0 : old) | 0x02);   /* add page scan */
        (void)hci_cmd_sync(l->hci, HB_OP_WRITE_SCAN_ENABLE, &se, 1, NULL, NULL, 0);
    }
    deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        if (btlink_pump(l, 40) < 0) break;
        if (btlink_abort_connect) {
            int stop = 0;
            for (k = 0; k < l->acc_n && !stop; k++) stop = btlink_abort_connect(l->acc_addr[k]);
            if (stop) {
                log_line("btlink: listening stopped for a page command");
                if (l->connected) {
                    unsigned char dp[3];
                    put16(dp, l->handle);
                    dp[2] = 0x13;
                    fire_cmd(l->hci, HB_OP_DISCONNECT, dp, 3);
                }
                break;
            }
        }
        if (l->connected && l->enc_on) { ok = 1; break; }
        if (l->acc_got && !l->connected && l->cc_fail) break;
        if (l->acc_got && l->connected && now_ms() - l->t_conn > 25000) {
            log_line("btlink: incoming link: auth/encrypt timeout");
            break;
        }
        if (l->acc_got && !l->connected && !l->cc_fail)
            deadline = deadline > now_ms() + 8000 ? deadline : now_ms() + 8000;
    }
    if (old >= 0 && !(old & 0x02)) {
        se = (unsigned char)old;
        (void)hci_cmd_sync(l->hci, HB_OP_WRITE_SCAN_ENABLE, &se, 1, NULL, NULL, 0);
    }
    if (ok) {
        if (which) *which = l->acc_got - 1;
        log_line("btlink: incoming link ready (encrypted)");
    }
    l->acc_n = 0;
    return ok;
}

int btlink_last_connect_fail(void)
{
    return g_connect_fail;
}

int btlink_connect(btlink *l, const unsigned char addr[6],
                   unsigned char psrm, unsigned clock_offset,
                   const unsigned char *link_key, unsigned char key_type,
                   char *name_inout, int name_max, int timeout_ms)
{
    unsigned char p[16];
    long t0, deadline;

    if (!l || !addr) return 0;
    memcpy(l->addr, addr, 6);
    l->connected = l->auth_sent = l->auth_ok = l->enc_sent = l->enc_on = 0;
    l->handle = 0;
    l->retry_create = 0;
    l->create_retries = 0;
    l->purge_fail = 0;
    l->need_drop = 0;
    l->cc_fail = 0;
    g_connect_fail = 0;
    l->drop_hint = 0;
    l->pending_disc = 0;
    l->pending_disc_done = 0;
    l->info_done = 0;
    l->txq_n = l->txq_head = l->txq_media = 0;
    l->pool.outstanding = 0;
    l->fresh_pair = (link_key == NULL);
    if (link_key) {
        memcpy(l->link_key, link_key, 16);
        l->key_type = key_type;
        l->have_key = 1;
    } else {
        l->have_key = 0;
    }
    if (name_inout && name_max > 0 && name_inout[0])
        snprintf(l->name, sizeof l->name, "%s", name_inout);
    else
        l->name[0] = 0;

    {
        char astr[18];
        hci_addr_str(addr, astr);
        log_line("btlink: connecting to %s (%s)", astr,
                 l->fresh_pair ? "fresh pair" : "stored key");
    }

    {
        /* Same page parameters the first (post-inquiry) connect used: the
         * repetition mode and clock offset last seen for this address. */
        unsigned char tp; unsigned tc;
        if (acl_track_page_params(addr, &tp, &tc)) {
            if (!psrm || psrm == 0x01) psrm = tp;
            if (!(clock_offset & 0x8000)) clock_offset = tc;
        }
    }
    {
        /* Explicit page timeout: 0x8000 slots = 20.48 s. */
        unsigned char pt[2] = { 0x00, 0x80 }, o[16]; int ol = 0;
        (void)hci_cmd_sync(l->hci, 0x0C18, pt, 2, o, &ol, (int)sizeof o);
    }
    memcpy(p, addr, 6);
    put16(p + 6, 0xCC18);
    p[8] = (psrm && psrm <= 2) ? psrm : 0x01;   /* never R0 */
    p[9] = 0;
    if (clock_offset & 0x8000)
        put16(p + 10, clock_offset);
    else
        put16(p + 10, 0);
    p[12] = 1;
    log_line("btlink: CREATE_CONNECTION params %02x:%02x:%02x:%02x:%02x:%02x %02x %02x %02x %02x %02x %02x %02x"
             " (pkt %04x psrm R%u clock %s %04x role-switch %u)",
             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12],
             0xCC18u, p[8], (clock_offset & 0x8000) ? "valid" : "none", clock_offset & 0x7FFF, p[12]);

    if (!l->hci.ops->cmd(l->hci.ctx, HB_OP_CREATE_CONNECTION, p, 13))
        return 0;

    t0 = now_ms();
    deadline = t0 + (timeout_ms > 0 ? timeout_ms : 45000);

    while (now_ms() < deadline) {
        if (btlink_pump(l, 40) < 0) return 0;

        /* After 0x0b: drop known handles, wait Disc Complete, settle, retry
         * CREATE once (max 2 attempts total). Never infinite errno-5 loop. */
        if (!l->connected && l->retry_create && l->create_retries < 1) {
            long w;
            l->retry_create = 0;
            if (l->need_drop) {
                unsigned hint = l->drop_hint;
                l->need_drop = 0;
                l->drop_hint = 0;
                if (drop_known_handles(l, hint) < 0) {
                    log_line("btlink: drop aborted — connect FAIL (stale ACL)");
                    return 0;
                }
            }
            if (l->purge_fail) {
                log_line("btlink: purge_fail set — refusing CREATE retry");
                return 0;
            }
            w = now_ms() + 500;
            while (now_ms() < w) {
                if (btlink_pump(l, 40) < 0) return 0;
            }
            l->create_retries++;
            log_line("btlink: retry CREATE_CONNECTION (attempt %d of 2)",
                     l->create_retries + 1);
            if (!l->hci.ops->cmd(l->hci.ctx, HB_OP_CREATE_CONNECTION, p, 13))
                return 0;
            t0 = now_ms();
        } else if (!l->connected && l->retry_create && l->create_retries >= 1) {
            log_line("btlink: 0x0b again after retry — FAIL (case-cycle buds?)");
            l->retry_create = 0;
            return 0;
        }

        if (btlink_abort_connect && btlink_abort_connect(l->addr)) {
            unsigned char dp[3];
            if (!l->connected) {
                fire_cmd(l->hci, 0x0408, l->addr, 6);    /* Create Connection Cancel */
                log_line("btlink: connect aborted — Create Connection cancelled");
            } else {
                put16(dp, l->handle);
                dp[2] = 0x13;
                fire_cmd(l->hci, HB_OP_DISCONNECT, dp, 3);
                log_line("btlink: connect aborted — link closed");
            }
            {
                long w = now_ms() + 300;
                while (now_ms() < w) if (btlink_pump(l, 40) < 0) break;
            }
            return 0;
        }
        if (!l->connected && l->cc_fail) {
            /* 0x04 page timeout etc.: no point waiting out the full timeout. */
            g_connect_fail = l->cc_fail;
            log_line("btlink: connect failed (status %#04x%s)", l->cc_fail,
                     l->cc_fail == 0x04 ? ", page timeout" : "");
            return 0;
        }
        if (l->connected && l->enc_on) {
            if (name_inout && name_max > 0 && l->name[0])
                snprintf(name_inout, (size_t)name_max, "%s", l->name);
            log_line("btlink: encrypted link ready");
            return 1;
        }
        if (!l->connected && now_ms() - t0 > 20000) {
            g_connect_fail = 0x04;          /* no answer: same as a page timeout */
            log_line("btlink: connection timeout");
            return 0;
        }
        if (l->connected && now_ms() - l->t_conn > 25000) {
            log_line("btlink: auth/encrypt timeout");
            return 0;
        }
    }
    log_line("btlink: overall timeout");
    return 0;
}

/* Drop only the ACL this app created (our handle, plus a handle we created
 * earlier that never saw Disconnection Complete). System links such as the
 * DualSense are never touched. Waits for Disconnection Complete so no stale
 * link (HCI 0x0b on next connect) is left behind. */
int btlink_drop_handle(btlink *l, unsigned handle, int wait_ms)
{
    int rc;
    if (!l || !handle) return 0;
    log_line("btlink: closing existing ACL %#05x before paging", handle & 0x0FFF);
    rc = disconnect_handle(l, handle & 0x0FFF, wait_ms);
    if (g_last_acl_handle == (handle & 0x0FFF)) g_last_acl_handle = 0;
    return rc;
}

void btlink_disconnect(btlink *l)
{
    unsigned h;
    int i;

    if (!l) return;
    for (i = 0; i < BTLINK_CHAN_MAX; i++)
        chan_set(&l->ch[i], CH_CLOSED);
    if (l->connected) {
        int rc;
        h = l->handle & 0x0FFF;
        rc = disconnect_handle(l, h, 2500);
        l->connected = 0;
        if (g_last_acl_handle == h) g_last_acl_handle = 0;
        g_close_confirmed = (rc == 1);
        log_line("btlink: own ACL %#05x closed — Disconnection Complete %s", h,
                 rc == 1 ? "confirmed" : "NOT confirmed");
    }
    if (g_last_acl_handle) {
        h = g_last_acl_handle;
        g_last_acl_handle = 0;
        log_line("btlink: dropping leftover own ACL %#05x", h);
        (void)disconnect_handle(l, h, 1500);
    }
}

/* Information Request exchange (extended features, then fixed channels)
 * before the first Connection Request, as a usual initiator does. */
static void info_exchange(btlink *l)
{
    static const unsigned types[2] = { 2, 3 };
    int i;
    if (l->info_done) return;
    l->info_done = 1;
    for (i = 0; i < 2; i++) {
        unsigned char r[2];
        long w;
        put16(r, types[i]);
        l->info_wait_id = next_sig_id(l);
        l->info_got = 0;
        log_line("l2cap: INFO_REQ type %u id=%u", types[i], l->info_wait_id);
        if (!sig_send(l, L2SIG_INFO_REQ, l->info_wait_id, r, 2)) break;
        w = now_ms() + 1500;
        while (now_ms() < w && !l->info_got)
            if (btlink_pump(l, 20) < 0) return;
        if (!l->info_got)
            log_line("l2cap: no INFO_RSP for type %u (continuing)", types[i]);
    }
    l->info_wait_id = 0;
}

unsigned btlink_chan_open(btlink *l, unsigned psm, int timeout_ms)
{
    chan *c;
    unsigned char r[4];
    long deadline;
    int sig_phase;

    if (!l || !btlink_is_up(l)) return 0;
    info_exchange(l);
    if (!btlink_is_up(l)) return 0;
    c = chan_free_slot(l);
    if (!c) {
        log_line("l2cap: no free channel slot");
        return 0;
    }
    {
        int i;
        sig_phase = (psm == BTLINK_PSM_AVDTP);
        for (i = 0; i < BTLINK_CHAN_MAX; i++)       /* media channel: not a race */
            if (&l->ch[i] != c && l->ch[i].psm == psm && l->ch[i].st == CH_OPEN) sig_phase = 0;
    }
    memset(c, 0, sizeof *c);
    c->psm = psm;
    c->inbound = 0;
    c->scid = l->next_scid++;
    if (l->next_scid < 0x0040) l->next_scid = 0x0040;
    chan_set(c, CH_CONNECTING);
    put16(r, psm);
    put16(r + 2, c->scid);
    sig_send(l, L2SIG_CONN_REQ, next_sig_id(l), r, 4);
    log_line("l2cap: opening PSM %#x (scid %#x)", psm, c->scid);

    deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 8000);
    while (now_ms() < deadline) {
        if (btlink_pump(l, 40) < 0) return 0;
        if (c->st == CH_OPEN) return c->scid;
        if (sig_phase) {
            /* Simultaneous open: the peer opened the same PSM to us. Its
             * channel wins (peers often never configure ours then). */
            int i;
            for (i = 0; i < BTLINK_CHAN_MAX; i++) {
                chan *in = &l->ch[i];
                if (in == c || !in->inbound || in->psm != psm) continue;
                if (in->st == CH_OPEN) {
                    log_line("l2cap: PSM %#x opened by the peer too — using its channel "
                             "(scid %#x), closing ours (scid %#x)", psm, in->scid, c->scid);
                    if (c->st != CH_CLOSED) chan_close_disc(l, c);
                    return in->scid;
                }
                if (in->st == CH_CONFIG && c->st == CH_CLOSED) {
                    /* ours died; give the peer's channel time to finish */
                    long w = now_ms() + 3000;
                    while (now_ms() < w && in->st == CH_CONFIG)
                        if (btlink_pump(l, 40) < 0) return 0;
                    if (in->st == CH_OPEN) return in->scid;
                }
            }
        }
        if (c->st == CH_CLOSED) return 0;
    }
    log_line("l2cap: open timeout PSM %#x", psm);
    chan_close_disc(l, c); /* P3: DISC before abandon */
    return 0;
}

void btlink_chan_close(btlink *l, unsigned scid)
{
    chan *c;
    if (!l) return;
    c = chan_by_scid(l, scid);
    if (!c) return;
    chan_close_disc(l, c);
}

int btlink_chan_is_open(const btlink *l, unsigned scid)
{
    const chan *c;
    if (!l) return 0;
    c = chan_by_scid((btlink *)l, scid);
    return c && c->st == CH_OPEN;
}

unsigned btlink_chan_find_inbound(const btlink *l, unsigned psm)
{
    int i;
    if (!l) return 0;
    for (i = 0; i < BTLINK_CHAN_MAX; i++)
        if (l->ch[i].st == CH_OPEN && l->ch[i].psm == psm && l->ch[i].inbound)
            return l->ch[i].scid;
    return 0;
}

unsigned btlink_chan_find_psm(const btlink *l, unsigned psm)
{
    int i;
    if (!l) return 0;
    for (i = 0; i < BTLINK_CHAN_MAX; i++) {
        if (l->ch[i].st == CH_OPEN && l->ch[i].psm == psm &&
            !l->ch[i].inbound)
            return l->ch[i].scid;
    }
    return 0;
}

void btlink_chan_close_psm(btlink *l, unsigned psm)
{
    int i;
    if (!l) return;
    for (i = 0; i < BTLINK_CHAN_MAX; i++) {
        if (l->ch[i].st != CH_CLOSED && l->ch[i].psm == psm) {
            log_line("l2cap: closing PSM %#x scid %#x (%s)",
                     psm, l->ch[i].scid,
                     l->ch[i].inbound ? "inbound" : "outbound");
            btlink_chan_close(l, l->ch[i].scid);
        }
    }
}

void btlink_close_inbound_sdp(btlink *l)
{
    int i, n = 0;
    if (!l) return;
    for (i = 0; i < BTLINK_CHAN_MAX; i++) {
        if (l->ch[i].st != CH_CLOSED &&
            l->ch[i].psm == BTLINK_PSM_SDP && l->ch[i].inbound) {
            log_line("l2cap: closing inbound SDP scid %#x (before media)",
                     l->ch[i].scid);
            btlink_chan_close(l, l->ch[i].scid);
            n++;
        }
    }
    if (n)
        log_line("l2cap: closed %d inbound SDP channel(s); AVDTP signaling kept",
                 n);
}

int btlink_l2_send(btlink *l, unsigned scid, const unsigned char *d, int len)
{
    chan *c;
    if (!l) return 0;
    c = chan_by_scid(l, scid);
    if (!c || c->st != CH_OPEN) return 0;
    return l2_send_raw(l, c->dcid, d, len);
}

void btlink_set_inbound_rx(btlink *l, unsigned psm, btlink_rx_fn fn, void *ud)
{
    if (!l) return;
    l->in_rx_psm = psm;
    l->in_rx = fn;
    l->in_rx_ud = ud;
}

static void replay_early(btlink *l, unsigned scid, btlink_rx_fn fn, void *ud)
{
    int i, k = 0;
    for (i = 0; i < l->n_early; i++) {
        if (l->early[i].scid == scid) {
            log_line("avdtp: answering the headset's early message (hdr %02x %02x)",
                     l->early[i].d[0], l->early[i].len > 1 ? l->early[i].d[1] : 0);
            fn(ud, scid, l->early[i].d, l->early[i].len);
        } else {
            l->early[k++] = l->early[i];
        }
    }
    l->n_early = k;
}

void btlink_set_cfg_timeout(btlink *l, long ms)
{
    if (l) l->cfg_timeout_ms = ms;
}

void btlink_set_rx(btlink *l, unsigned scid, btlink_rx_fn fn, void *ud)
{
    chan *c;
    if (!l) return;
    c = chan_by_scid(l, scid);
    if (!c) return;
    c->rx = fn;
    c->rx_ud = ud;
    if (fn && l->n_early) replay_early(l, scid, fn, ud);
}

int btlink_wait_rx(btlink *l, unsigned scid, unsigned char *out, int max,
                   int timeout_ms)
{
    chan *c;
    long deadline;

    if (!l || !out || max <= 0) return 0;
    c = chan_by_scid(l, scid);
    if (!c || c->st != CH_OPEN) return 0;
    c->wait_buf = out;
    c->wait_max = max;
    c->wait_len = 0;
    c->wait_armed = 1;
    deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 3000);
    while (now_ms() < deadline) {
        if (btlink_pump(l, 40) < 0) break;
        if (!c->wait_armed) {
            int n = c->wait_len;
            c->wait_buf = NULL;
            return n;
        }
    }
    c->wait_armed = 0;
    c->wait_buf = NULL;
    return 0;
}

unsigned btlink_chan_peer_mtu(const btlink *l, unsigned scid)
{
    int i;
    if (!l) return 0;
    for (i = 0; i < BTLINK_CHAN_MAX; i++)
        if (l->ch[i].st != CH_CLOSED && l->ch[i].scid == scid)
            return l->ch[i].peer_mtu;
    return 0;
}

int btlink_l2_send_media(btlink *l, unsigned scid, const unsigned char *d, int len)
{
    int r;
    if (!l) return 0;
    l->tx_is_media = 1;
    r = btlink_l2_send(l, scid, d, len);
    l->tx_is_media = 0;
    return r;
}

long btlink_ms_since_credit(const btlink *l)
{
    if (!l || !l->pool.cred_evt || l->pool.outstanding <= 0) return 0;
    return now_ms() - l->pool.last_credit_ms;
}

long btlink_acl_gap_avg(const btlink *l)
{
    return l ? l->pool.gap_avg : 0;
}

long btlink_tx_dropped(const btlink *l)
{
    return l ? (long)l->pool.media_dropped : 0;
}

int btlink_tx_queue_max(void)
{
    return MEDIA_Q_MAX;
}

int btlink_media_cap(const btlink *l)
{
    return l && l->media_cap > 0 ? l->media_cap : MEDIA_Q_DEFAULT;
}

void btlink_set_media_cap(btlink *l, int n)
{
    if (!l) return;
    if (n < HB_QUEUE_FLOOR_PKTS) n = HB_QUEUE_FLOOR_PKTS;
    if (n > MEDIA_Q_MAX) n = MEDIA_Q_MAX;
    l->media_cap = n;
}

void btlink_set_media_pace(btlink *l, long ms_per_packet)
{
    if (l) l->media_dur_ms = ms_per_packet;
}

void btlink_tx_counters(const btlink *l, unsigned long *sent, unsigned long *credits,
                        int *limit)
{
    if (!l) return;
    if (sent) *sent = l->pool.sent;
    if (credits) *credits = l->pool.cred_evt + l->pool.cred_fb;
    if (limit) *limit = l->pool.limit;
}

int btlink_tx_backlog(const btlink *l)
{
    return l ? l->txq_n : 0;
}

/* ---- AVRCP absolute volume API ------------------------------------- */

static int avrcp_open(const btlink *l)
{
    int i;
    if (!l || !l->avrcp_scid) return 0;
    for (i = 0; i < BTLINK_CHAN_MAX; i++)
        if (l->ch[i].scid == l->avrcp_scid && l->ch[i].st == CH_OPEN) return 1;
    return 0;
}

void btlink_avrcp_set_volume(btlink *l, int vol)
{
    unsigned char r[32];
    int n;
    if (!l) return;
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    if (!avrcp_open(l)) {
        l->avrcp.volume = vol;
        return;
    }
    n = avrcp_build_set_volume(&l->avrcp, vol, r, (int)sizeof r);
    avrcp_send(l, r, n);                       /* we as controller */
    n = avrcp_build_volume_changed(&l->avrcp, r, (int)sizeof r);
    avrcp_send(l, r, n);                       /* we as target */
    log_line("avrcp: SetAbsoluteVolume %d/127 sent", vol);
}

int btlink_avrcp_volume(btlink *l, int *changed)
{
    if (changed) *changed = 0;
    if (!l) return -1;
    if (changed && l->avrcp.changed) {
        *changed = 1;
        l->avrcp.changed = 0;
    }
    return l->avrcp.volume;
}

int btlink_avrcp_state(const btlink *l)
{
    int st = 0;
    if (!l) return 0;
    if (avrcp_open(l)) st |= 1;
    if (l->avrcp.remote_abs) st |= 2;
    if (l->avrcp.ct_registered || l->avrcp.notify_label >= 0) st |= 4;
    if (l->avrcp.sink_renders) st |= 8;
    return st;
}

int btlink_avrcp_connect(btlink *l)
{
    if (!l || avrcp_open(l)) return 1;
    l->avrcp_scid = 0;
    return btlink_chan_open(l, BTLINK_PSM_AVCTP, 4000) != 0;
}

int btlink_last_close_confirmed(void)
{
    return g_close_confirmed;
}

int btlink_own_acl_pending(void)
{
    return g_last_acl_handle != 0;
}
