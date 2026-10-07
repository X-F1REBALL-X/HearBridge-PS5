#include "sdp_a2dp.h"
#include "log.h"
#include "util.h"

#include <string.h>

/* Set by sdp_search_once when the remote answered with no 0x110B record. */
static int g_sdp_no_sink;
/* Set when SDP never answered and the fixed AVDTP PSM was assumed. */
static int g_sdp_fallback;

/* Walk SDP data elements; find L2CAP UUID 0x0100 followed by uint16 PSM. */
static int find_l2cap_psm(const unsigned char *p, int n, unsigned *out_psm)
{
    int i = 0;
    int after_l2cap = 0;

    while (i < n) {
        int type, size, hl, clen;
        if (i >= n) break;
        type = p[i] >> 3;
        size = p[i] & 7;
        if (type == 0) { i += 1; after_l2cap = 0; continue; }
        if (size < 5) {
            static const int fixed[5] = {1,2,4,8,16};
            clen = fixed[size];
            hl = 1;
        } else if (size == 5) {
            if (i + 1 >= n) return 0;
            clen = p[i + 1];
            hl = 2;
        } else if (size == 6) {
            if (i + 2 >= n) return 0;
            clen = (int)be16(p + i + 1);
            hl = 3;
        } else {
            if (i + 4 >= n) return 0;
            clen = (int)be32(p + i + 1);
            hl = 5;
        }
        if (hl + clen > n - i) return 0;

        if (type == 6 || type == 7) {
            if (find_l2cap_psm(p + i + hl, clen, out_psm)) return 1;
            after_l2cap = 0;
        } else if (type == 3 && clen == 2) { /* UUID16 */
            unsigned uuid = be16(p + i + hl);
            after_l2cap = (uuid == 0x0100);
        } else if (type == 1 && clen == 2 && after_l2cap) { /* uint16 PSM */
            *out_psm = be16(p + i + hl);
            return 1;
        } else {
            after_l2cap = 0;
        }
        i += hl + clen;
    }
    return 0;
}

/* Open a fresh *outbound* SDP L2CAP. Never reuse an inbound PSM 0x1 —
 * that channel is the headset querying *us* (we are the SDP server there).
 * 0.1.1 reused inbound → ServiceSearch timed out with no response. */
static unsigned sdp_open_fresh(btlink *link)
{
    unsigned scid;
    int attempt;

    /* Tear down any half-open / inbound SDP so the remote is not confused. */
    btlink_chan_close_psm(link, BTLINK_PSM_SDP);
    {
        long w = now_ms() + 300;
        while (now_ms() < w)
            if (btlink_pump(link, 40) < 0) return 0;
    }

    for (attempt = 0; attempt < 2; attempt++) {
        if (!btlink_is_up(link)) { log_line("sdp: link is down — stop"); return 0; }
        if (attempt) {
            log_line("sdp: reopen PSM 0x0001 (attempt %d)", attempt + 1);
            btlink_chan_close_psm(link, BTLINK_PSM_SDP);
            {
                long w = now_ms() + 400;
                while (now_ms() < w)
                    if (btlink_pump(link, 40) < 0) return 0;
            }
        }
        scid = btlink_chan_open(link, BTLINK_PSM_SDP, 10000);
        if (scid) {
            log_line("sdp: outbound PSM 0x0001 open (scid %#x)", scid);
            return scid;
        }
        log_line("sdp: outbound open failed (attempt %d)", attempt + 1);
    }
    return 0;
}

static unsigned sdp_search_once(btlink *link, unsigned scid, int timeout_ms)
{
    unsigned char req[64];
    unsigned char rsp[1024];
    unsigned char acc[2048];
    int acc_len = 0;
    unsigned tid = 1;
    long deadline;
    unsigned psm = 0;
    int tries = 0;
    unsigned char cont[16];
    int cont_len = 0;

    /* ServiceSearchAttribute: Audio Sink 0x110B, attrs ProtocolDescriptorList */
    static const unsigned char body[] = {
        0x35, 0x03, 0x19, 0x11, 0x0B,   /* search pattern */
        0xFF, 0xFF,                     /* max attr bytes */
        0x35, 0x05, 0x0A, 0x00, 0x04, 0x00, 0x09, /* attr range 0x0004-0x0009 */
    };

    deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 15000);

    while (now_ms() < deadline && tries < 6) {
        int n = 5;
        int got;

        if (!btlink_chan_is_open(link, scid)) {
            log_line("sdp: channel closed mid-search");
            return 0;
        }

        memcpy(req + n, body, sizeof body);
        n += (int)sizeof body;
        req[n++] = (unsigned char)cont_len;
        if (cont_len) {
            memcpy(req + n, cont, (size_t)cont_len);
            n += cont_len;
        }
        tid = (tid + 1) & 0xFFFF;
        if (!tid) tid = 1;
        req[0] = 0x06; /* ServiceSearchAttributeRequest */
        req[1] = (unsigned char)(tid >> 8);
        req[2] = (unsigned char)tid;
        req[3] = (unsigned char)((n - 5) >> 8);
        req[4] = (unsigned char)(n - 5);

        if (!btlink_l2_send(link, scid, req, n)) {
            log_line("sdp: send failed");
            return 0;
        }
        tries++;

        /* Longer per-try wait — some sinks are slow after reconnect. */
        got = btlink_wait_rx(link, scid, rsp, (int)sizeof rsp, 5000);
        if (got < 5) {
            log_line("sdp: no response (try %d)", tries);
            continue;
        }
        if (be16(rsp + 1) != tid) {
            log_line("sdp: tid mismatch (got %#x want %#x)", be16(rsp + 1), tid);
            continue;
        }
        if (rsp[0] != 0x07 || got < 8) {
            log_line("sdp: unexpected PDU %#04x len %d", rsp[0], got);
            return 0;
        }
        {
            int bytes = (int)be16(rsp + 5);
            int cl;
            if (7 + bytes > got) {
                log_line("sdp: truncated attribute list");
                return 0;
            }
            if (acc_len + bytes > (int)sizeof acc) {
                log_line("sdp: attribute buffer full");
                return 0;
            }
            memcpy(acc + acc_len, rsp + 7, (size_t)bytes);
            acc_len += bytes;
            cl = rsp[7 + bytes];
            if (cl > 0 && 8 + bytes + cl <= got && cl <= (int)sizeof cont) {
                memcpy(cont, rsp + 8 + bytes, (size_t)cl);
                cont_len = cl;
                tries = 0;
                continue;
            }
            cont_len = 0;
        }
        log_line("sdp: search+attributes -> %d bytes", acc_len);
        /* Empty AttributeLists (35 00 / no bytes): remote has no A2DP Sink. */
        if (acc_len <= 2) {
            log_line("sdp: no Audio Sink (0x110B) record");
            g_sdp_no_sink = 1;
            return 0;
        }
        if (find_l2cap_psm(acc, acc_len, &psm) && psm) {
            log_line("sdp: AVDTP L2CAP PSM %#x", psm);
            return psm;
        }
        log_line("sdp: no L2CAP PSM in attrs — assuming 0x0019");
        return BTLINK_PSM_AVDTP;
    }
    return 0;
}

int sdp_one_round;

unsigned sdp_find_avdtp_psm(btlink *link, int timeout_ms)
{
    unsigned scid;
    unsigned psm = 0;
    int round;
    int per_round;

    if (!link || !btlink_is_up(link)) {
        log_line("sdp: link not up");
        return 0;
    }

    /* Ignore any "already open" inbound SDP — open clean outbound. */
    if (btlink_chan_find_psm(link, BTLINK_PSM_SDP))
        log_line("sdp: ignoring leftover outbound PSM 0x1 — reopening fresh");

    per_round = timeout_ms > 0 ? timeout_ms / 2 : 12000;
    if (per_round < 8000) per_round = 8000;

    g_sdp_no_sink = 0;
    g_sdp_fallback = 0;
    if (sdp_one_round && timeout_ms > 0) per_round = timeout_ms;
    for (round = 0; round < (sdp_one_round ? 1 : 2) && !psm && !g_sdp_no_sink &&
                    btlink_is_up(link); round++) {
        if (round)
            log_line("sdp: fresh L2CAP retry after search timeout");
        scid = sdp_open_fresh(link);
        if (!scid) {
            log_line("sdp: cannot open outbound PSM 0x0001");
            continue;
        }
        psm = sdp_search_once(link, scid, per_round);
        btlink_chan_close(link, scid);
        if (!psm) {
            long w = now_ms() + 400;
            while (now_ms() < w)
                if (btlink_pump(link, 40) < 0) break;
        }
    }

    if (!psm && g_sdp_no_sink) return 0;
    if (!psm && !btlink_is_up(link)) return 0;     /* link gone: not a fallback case */
    if (!psm) {
        /* P5 (Elf): assigned AVDTP PSM is fixed by A2DP spec — unblock stream
         * when SDP L2CAP Config stays stuck (observed: ours=1 theirs=0). */
        g_sdp_fallback = 1;
        log_line("sdp: FALLBACK PSM 0x0019 (config stuck / SDP open failed)");
        return BTLINK_PSM_AVDTP;
    }
    return psm;
}

int sdp_probe_a2dp_sink(btlink *link, int timeout_ms, unsigned *psm)
{
    unsigned p = sdp_find_avdtp_psm(link, timeout_ms);
    if (psm) *psm = p;
    if (g_sdp_no_sink) return 0;
    if (!p || g_sdp_fallback) return -1;
    return 1;
}
