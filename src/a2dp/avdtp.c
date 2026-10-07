#include "avdtp.h"
#include "log.h"
#include "util.h"

#include <string.h>
#include <stdio.h>

/* AVDTP signal IDs */
#define AV_DISCOVER            0x01
#define AV_GET_CAPABILITIES    0x02
#define AV_SET_CONFIGURATION   0x03
#define AV_OPEN                0x06
#define AV_START               0x07
#define AV_CLOSE               0x08
#define AV_SUSPEND             0x09
#define AV_ABORT               0x0A
#define AV_GET_ALL_CAPABILITIES 0x0C

/* AVDTP signal header octet 0 (Bluetooth AVDTP Spec):
 *   bits 7-4: transaction label
 *   bits 3-2: packet type (0=single, 1=start, 2=continue, 3=end)
 *   bits 1-0: message type (0=command, 1=gen reject, 2=accept, 3=reject)
 * 0.1.0 swapped packet vs message → Accept (msg=2) was read as msg=0. */
#define AV_MSG_COMMAND 0
#define AV_MSG_GEN_REJ 1
#define AV_MSG_ACCEPT  2
#define AV_MSG_REJECT  3

#define AV_PKT_SINGLE  0
#define AV_PKT_START   1
#define AV_PKT_CONT    2
#define AV_PKT_END     3

#define AV_HDR(label, pkt, msg) \
    ((unsigned char)((((label) & 0x0F) << 4) | (((pkt) & 0x03) << 2) | ((msg) & 0x03)))

static unsigned char next_label(avdtp_session *s)
{
    s->label = (unsigned char)((s->label + 1) & 0x0F);
    if (!s->label) s->label = 1;
    return s->label;
}

/* Response wait per transmission. The spec's RTX lets an ACP take
 * well over 2.5 s on OPEN; resending sooner only earns a BAD_STATE reject
 * for the duplicate, so wait ~5 s before a single resend. */
#define AV_RSP_TIMEOUT_MS 5000
#define AV_CMD_TRIES      2
#define AV_ERR_BAD_STATE  0x31

/* Answer a command the sink sends while we wait for our own response, so
 * a sink-initiated DISCOVER / GET_CAPABILITIES / ABORT neither stalls the
 * sink nor breaks our wait. Response uses the sink's transaction label. */
static void avdtp_answer_remote(avdtp_session *s, unsigned scid,
                                const unsigned char *cmd, int len)
{
    unsigned char r[24];
    unsigned char rlabel = (cmd[0] >> 4) & 0x0F;
    unsigned char sig = cmd[1] & 0x3F;
    int n = 2;

    r[1] = sig;
    switch (sig) {
    case AV_DISCOVER:
        r[0] = AV_HDR(rlabel, AV_PKT_SINGLE, AV_MSG_ACCEPT);
        r[n++] = (unsigned char)(((s->int_seid & 0x3F) << 2) |
                                 (s->configured ? 0x02 : 0x00));
        r[n++] = 0x00; /* Audio, TSEP = SRC */
        break;
    case AV_GET_CAPABILITIES:
    case AV_GET_ALL_CAPABILITIES:
        r[0] = AV_HDR(rlabel, AV_PKT_SINGLE, AV_MSG_ACCEPT);
        r[n++] = 0x01; r[n++] = 0x00;          /* Media Transport */
        r[n++] = 0x07; r[n++] = 0x06;          /* Media Codec */
        r[n++] = 0x00; r[n++] = 0x00;          /* Audio, SBC */
        r[n++] = 0x3F; r[n++] = 0xFF;          /* 48/44.1/32/16, all modes/blocks */
        r[n++] = 2;    r[n++] = 53;            /* bitpool range */
        break;
    case AV_ABORT:
        /* Abort has no reject; always accept. Our pending command will then
         * be answered or time out on its own. */
        r[0] = AV_HDR(rlabel, AV_PKT_SINGLE, AV_MSG_ACCEPT);
        log_line("avdtp: sink sent ABORT");
        if (s->streaming) s->remote_closed = 1;
        break;
    case 0x04: /* GET_CONFIGURATION: our current config */
        if (!s->configured) goto bad_state;
        r[0] = AV_HDR(rlabel, AV_PKT_SINGLE, AV_MSG_ACCEPT);
        r[n++] = 0x01; r[n++] = 0x00;
        r[n++] = 0x07; r[n++] = 0x06;
        r[n++] = 0x00; r[n++] = 0x00;
        memcpy(r + n, s->sbc_cfg, 4); n += 4;
        break;
    case AV_START:     /* sink-initiated start / resume after its suspend */
    case AV_SUSPEND:
        if (!s->media_scid) goto bad_state;
        r[0] = AV_HDR(rlabel, AV_PKT_SINGLE, AV_MSG_ACCEPT);
        log_line("avdtp: sink sent %s", sig == AV_START ? "START" : "SUSPEND");
        break;
    case AV_CLOSE:
        if (!s->configured) goto bad_state;
        r[0] = AV_HDR(rlabel, AV_PKT_SINGLE, AV_MSG_ACCEPT);
        log_line("avdtp: sink sent CLOSE");
        s->remote_closed = 1;
        break;
    case 0x0D: /* DELAY_REPORT: just acknowledge */
        r[0] = AV_HDR(rlabel, AV_PKT_SINGLE, AV_MSG_ACCEPT);
        if (len >= 5)
            log_line("avdtp: sink delay report %u.%u ms",
                     (unsigned)((cmd[3] << 8) | cmd[4]) / 10,
                     (unsigned)((cmd[3] << 8) | cmd[4]) % 10);
        break;
    case AV_SET_CONFIGURATION:
        r[0] = AV_HDR(rlabel, AV_PKT_SINGLE, AV_MSG_REJECT);
        r[n++] = 0x00;                          /* service category */
        r[n++] = AV_ERR_BAD_STATE;
        break;
    case AV_OPEN:
    case 0x05: /* RECONFIGURE */
    bad_state:
    case 0x0B: /* SECURITY_CONTROL */
        r[0] = AV_HDR(rlabel, AV_PKT_SINGLE, AV_MSG_REJECT);
        if (sig == AV_START || sig == AV_SUSPEND)
            r[n++] = (len > 2) ? cmd[2] : 0;    /* failing SEID */
        if (sig == 0x05) r[n++] = 0x00;
        r[n++] = AV_ERR_BAD_STATE;
        break;
    default:
        r[0] = AV_HDR(rlabel, AV_PKT_SINGLE, AV_MSG_GEN_REJ);
        break;
    }
    log_line("avdtp: answered sink signal %#04x (label %u, msg=%u)",
             sig, rlabel, r[0] & 0x03);
    (void)btlink_l2_send(s->link, scid, r, n);
}

/* Signaling traffic outside our own command waits (e.g. while streaming):
 * answer the sink's commands so it never stalls on us. */
static void avdtp_sig_rx(void *ud, unsigned scid, const unsigned char *d, int len)
{
    avdtp_session *s = (avdtp_session *)ud;
    if (!s || len < 2) return;
    if (s->in_cmd && scid == s->sig_scid) return;  /* avdtp_cmd handles it */
    if ((d[0] & 0x03) == AV_MSG_COMMAND &&
        (((d[0] >> 2) & 0x03) == AV_PKT_SINGLE)) {
        avdtp_answer_remote(s, scid, d, len);
        return;
    }
    if (scid != s->sig_scid)
        log_line("avdtp: inbound AVDTP scid %#x: %d bytes (hdr %02x %02x) ignored",
                 scid, len, d[0], d[1]);
}

/* Send command, wait for matching response. Copies response payload (after
 * 2-byte header) into out. Returns message type (2=accept) or -1.
 * A resend keeps the same transaction label, so a late response to the
 * first transmission still matches. If the resend is rejected with
 * BAD_STATE, the first one was evidently accepted: treat as accept. */
static int avdtp_cmd_inner(avdtp_session *s, unsigned char signal,
                     const unsigned char *payload, int plen,
                     unsigned char *out, int out_max, int *out_len)
{
    unsigned char pkt[64];
    unsigned char rsp[256];
    unsigned char label = next_label(s);
    int n = 2 + (plen > 0 ? plen : 0);
    int tries, got;
    long deadline;

    if (n > (int)sizeof pkt) return -1;
    if (out_len) *out_len = 0;
    pkt[0] = AV_HDR(label, AV_PKT_SINGLE, AV_MSG_COMMAND);
    pkt[1] = (unsigned char)(signal & 0x3F);
    if (plen > 0) memcpy(pkt + 2, payload, (size_t)plen);

    for (tries = 0; tries < (s->quick ? 1 : AV_CMD_TRIES); tries++) {
        if (!btlink_l2_send(s->link, s->sig_scid, pkt, n)) {
            log_line("avdtp: send signal %#04x failed", signal);
            return -1;
        }
        deadline = now_ms() + (s->quick ? 1200 : AV_RSP_TIMEOUT_MS);
        while (now_ms() < deadline) {
            got = btlink_wait_rx(s->link, s->sig_scid, rsp, (int)sizeof rsp, 500);
            if (got < 2) {
                if (!btlink_chan_is_open(s->link, s->sig_scid)) {
                    log_line("avdtp: signaling closed waiting for %#04x", signal);
                    return -1;
                }
                continue;
            }
            {
                unsigned char rlabel = (rsp[0] >> 4) & 0x0F;
                unsigned char pkt_type = (rsp[0] >> 2) & 0x03;
                unsigned char msg = rsp[0] & 0x03;
                unsigned char sig = rsp[1] & 0x3F;
                if (pkt_type != AV_PKT_SINGLE && pkt_type != AV_PKT_END) {
                    log_line("avdtp: multi-packet type %u (len %d) ignored",
                             pkt_type, got);
                    continue;
                }
                if (msg == AV_MSG_COMMAND) {
                    avdtp_answer_remote(s, s->sig_scid, rsp, got);
                    continue;
                }
                if (rlabel != label) {
                    log_line("avdtp: stale response label %u sig %#04x msg=%u dropped",
                             rlabel, sig, msg);
                    continue;
                }
                if (msg != AV_MSG_GEN_REJ && sig != signal) {
                    log_line("avdtp: unexpected signal %#04x (wanted %#04x) msg=%u",
                             sig, signal, msg);
                    continue;
                }
                if (out && out_len) {
                    int body = got - 2;
                    if (body > out_max) body = out_max;
                    if (body > 0) memcpy(out, rsp + 2, (size_t)body);
                    *out_len = body > 0 ? body : 0;
                }
                if (msg == AV_MSG_ACCEPT) {
                    log_line("avdtp: signal %#04x accept (%d bytes)%s", signal,
                             got - 2, tries ? " after resend" : "");
                    return AV_MSG_ACCEPT;
                }
                if (msg == AV_MSG_REJECT && tries > 0 && got > 2 &&
                    rsp[got - 1] == AV_ERR_BAD_STATE) {
                    log_line("avdtp: signal %#04x resend got BAD_STATE — first one "
                             "was accepted, continuing", signal);
                    if (out_len) *out_len = 0;
                    return AV_MSG_ACCEPT;
                }
                log_line("avdtp: signal %#04x rejected (msg=%u body0=%#04x)",
                         signal, msg, (got > 2) ? rsp[2] : 0);
                return (int)msg;
            }
        }
        log_line("avdtp: signal %#04x unanswered after %d ms (try %d)", signal,
                 AV_RSP_TIMEOUT_MS, tries + 1);
        /* Resend with the SAME label: the response to either copy matches. */
    }
    return -1;
}

static int avdtp_cmd(avdtp_session *s, unsigned char signal,
                     const unsigned char *payload, int plen,
                     unsigned char *out, int out_max, int *out_len)
{
    int r;
    s->in_cmd = 1;
    r = avdtp_cmd_inner(s, signal, payload, plen, out, out_max, out_len);
    s->in_cmd = 0;
    return r;
}

static void log_hex_prefix(const char *tag, const unsigned char *p, int n, int maxn)
{
    char line[160];
    int i, pos = 0, show = n, truncated = 0;
    if (show > maxn) { show = maxn; truncated = 1; }
    for (i = 0; i < show && pos < (int)sizeof line - 4; i++)
        pos += snprintf(line + pos, sizeof line - (size_t)pos, "%02x%s",
                        p[i], (i + 1 < show) ? " " : "");
    log_line("%s (%d): %s%s", tag, n, line, truncated ? "…" : "");
}

/* Media Codec service category (0x07): look for Audio + SBC (0x00). */
static int parse_sbc_caps(const unsigned char *caps, int len, avdtp_sink_info *sink)
{
    int i = 0;
    while (i + 1 < len) {
        unsigned char cat = caps[i];
        unsigned char clen = caps[i + 1];
        if (i + 2 + clen > len) break;
        if (cat == 0x07 && clen >= 2) {
            unsigned char mt = caps[i + 2];
            unsigned char ct = caps[i + 3];
            unsigned char media = (unsigned char)((mt >> 4) & 0x0F);
            log_line("avdtp: MediaCodec media=%u codec=%#04x losc=%u",
                     media, ct, clen);
            if (media == 0 && ct == 0 && clen >= 6) { /* Audio + SBC */
                memcpy(sink->sbc_caps, caps + i + 4, 4);
                sink->have_sbc = 1;
                sink->bitpool_min = caps[i + 6];
                sink->bitpool_max = caps[i + 7];
                return 1;
            }
        }
        i += 2 + clen;
    }
    return 0;
}

/* AVDTP Discover SEP (2 octets), Spec:
 *   octet0: RFA[0] | InUse[1] | SEID[7:2]
 *   octet1: RFA[2:0] | TSEP[3] | MediaType[7:4]
 *   TSEP 0=SRC 1=SNK; MediaType 0=Audio.
 * 0.1.3 wrongly read TSEP from octet0 bit0 (RFA) → every SEP looked like SRC. */
static void parse_sep(const unsigned char *b, int *seid, int *in_use,
                      int *tsep, int *media)
{
    *seid = (b[0] >> 2) & 0x3F;
    *in_use = (b[0] >> 1) & 1;
    *tsep = (b[1] >> 3) & 1;
    *media = (b[1] >> 4) & 0x0F;
}

static int get_caps_for_seid(avdtp_session *s, int seid,
                             unsigned char *rsp, int rsp_max, int *rsp_len)
{
    unsigned char body[1];
    int msg;
    body[0] = (unsigned char)((seid & 0x3F) << 2);
    msg = avdtp_cmd(s, AV_GET_ALL_CAPABILITIES, body, 1, rsp, rsp_max, rsp_len);
    if (msg != AV_MSG_ACCEPT) {
        log_line("avdtp: GetAllCapabilities SEID %d msg=%d — try GetCapabilities",
                 seid, msg);
        msg = avdtp_cmd(s, AV_GET_CAPABILITIES, body, 1, rsp, rsp_max, rsp_len);
    }
    return msg;
}

static int pick_sbc_config(avdtp_sink_info *sink, uint8_t cfg[4], int *bitpool)
{
    unsigned char c0 = sink->sbc_caps[0];
    unsigned char c1 = sink->sbc_caps[1];
    int rate = 0, ch = 0, joint = 0;
    unsigned char out0 = 0, out1 = 0;
    int bp;

    /* freq: bit7=16k bit6=32k bit5=44.1 bit4=48 — pick 48 then 44.1 */
    if (c0 & 0x10) { out0 |= 0x10; rate = 48000; }
    else if (c0 & 0x20) { out0 |= 0x20; rate = 44100; }
    else {
        log_line("sbc: the headset takes neither 44.1 nor 48 kHz");
        return 0;
    }

    /* channel: bit3=mono bit2=dual bit1=stereo bit0=joint */
    if (c0 & 0x01) { out0 |= 0x01; ch = 2; joint = 1; }
    else if (c0 & 0x02) { out0 |= 0x02; ch = 2; }
    else if (c0 & 0x04) { out0 |= 0x04; ch = 2; }
    else if (c0 & 0x08) { out0 |= 0x08; ch = 1; }
    else {
        log_line("sbc: the headset takes no stereo mode");
        return 0;
    }

    /* A2DP 4.3.2: block length bit7=4 bit6=8 bit5=12 bit4=16; subbands
     * bit3=4 bit2=8; allocation bit1=SNR bit0=Loudness. Prefer 16 blocks,
     * 8 subbands, Loudness: 16*8 = 128 samples/frame, 375 frames/s at
     * 48 kHz. (Earlier builds took bit7 as 16 and so asked for 4 blocks:
     * 1500 tiny frames/s, ~4x the packet rate and no audio.) */
    if (c1 & 0x10) out1 |= 0x10;
    else if (c1 & 0x20) out1 |= 0x20;
    else if (c1 & 0x40) out1 |= 0x40;
    else out1 |= 0x80;

    if (c1 & 0x04) out1 |= 0x04;      /* 8 subbands */
    else out1 |= 0x08;                /* 4 subbands */

    if (c1 & 0x01) out1 |= 0x01;      /* Loudness */
    else out1 |= 0x02;                /* SNR */

    /* Start at bitpool 35 (~250 kbit/s at 48 kHz joint stereo). The
     * configuration carries a bitpool RANGE (A2DP 4.3.2.6: octets 2-3 are
     * min / max) so the stream can step the bitpool down when the radio
     * cannot keep up, and back up later; every SBC frame header carries the
     * bitpool in use. */
    {
        int lo = sink->bitpool_min, hi = sink->bitpool_max;
        if (lo < 2) lo = 2;
        if (hi > 53 || hi < lo) hi = hi < lo ? lo : 53;
        bp = 35;
        if (bp > hi) bp = hi;
        if (bp < lo) bp = lo;
        cfg[2] = (unsigned char)lo;
        cfg[3] = (unsigned char)hi;
    }

    cfg[0] = out0;
    cfg[1] = out1;

    sink->sample_rate = rate;
    sink->channels = ch;
    sink->joint_stereo = joint;
    *bitpool = bp;
    return 1;
}

int avdtp_setup(avdtp_session *s, btlink *link, unsigned avdtp_psm)
{
    unsigned char body[64], rsp[256];
    int rsp_len = 0;
    int i, nseid;
    int msg;

    memset(s, 0, sizeof *s);
    s->link = link;
    s->psm = avdtp_psm ? avdtp_psm : AVDTP_PSM;
    s->int_seid = 1; /* our Source SEID */
    s->rtp_ssrc = 0x48524247; /* 'HRBG' */
    s->rtp_seq = 1;
    s->rtp_ts = 0;

    /* The headset may open AVDTP signalling to us first: use that
     * channel instead of opening a second one (which it refuses). */
    {
        long w = now_ms() + 300;
        while (!(s->sig_scid = btlink_chan_find_inbound(link, s->psm)) && now_ms() < w)
            if (btlink_pump(link, 30) < 0) break;
    }
    if (s->sig_scid) {
        log_line("avdtp: using the headset's own signalling channel (scid %#x)", s->sig_scid);
        s->peer_opened = 1;
    } else
        s->sig_scid = btlink_chan_open(link, s->psm, 12000);
    if (!s->sig_scid) {
        unsigned in = btlink_chan_find_inbound(link, s->psm);
        if (in) {
            log_line("avdtp: our signalling refused; using the headset's (scid %#x)", in);
            s->sig_scid = in;
        }
    }
    if (!s->sig_scid) {
        log_line("avdtp: signaling channel failed");
        return 0;
    }

    btlink_set_rx(link, s->sig_scid, avdtp_sig_rx, s);   /* answers anything it sent early */
    btlink_set_inbound_rx(link, BTLINK_PSM_AVDTP, avdtp_sig_rx, s);
    if (s->peer_opened) {
        /* The headset opened signalling: it may drive first (Discover /
         * Get Capabilities). Answer it for ~1 s before our own Discover. */
        long w = now_ms() + 1000;
        while (now_ms() < w)
            if (btlink_pump(link, 30) < 0) break;
    }

    /* Discover (we are INT / Source; headset is ACP / Sink) */
    log_line("avdtp: Discover on signaling scid %#x", s->sig_scid);
    msg = avdtp_cmd(s, AV_DISCOVER, NULL, 0, rsp, (int)sizeof rsp, &rsp_len);
    if (msg != AV_MSG_ACCEPT || rsp_len < 2) {
        log_line("avdtp: discover failed (msg=%d len=%d)", msg, rsp_len);
        return 0;
    }
    log_line("avdtp: discover %d SEID(s)", rsp_len / 2);
    log_hex_prefix("avdtp: discover raw", rsp, rsp_len, 32);

    nseid = rsp_len / 2;
    {
        /* Keep Discover list — GetCaps overwrites rsp. */
        unsigned char disc[64];
        unsigned char caps[256];
        int caps_len = 0;
        int fallback_seid = -1;
        int picked = 0;

        if (rsp_len > (int)sizeof disc) rsp_len = (int)sizeof disc;
        memcpy(disc, rsp, (size_t)rsp_len);
        nseid = rsp_len / 2;
        s->sink.seid = -1;
        s->sink.have_sbc = 0;

        for (i = 0; i < nseid; i++) {
            int seid, in_use, tsep, media;
            parse_sep(disc + i * 2, &seid, &in_use, &tsep, &media);
            log_line("avdtp: SEP[%d] seid=%d in_use=%d tsep=%s media=%u raw=%02x %02x",
                     i, seid, in_use, tsep ? "SNK" : "SRC", media,
                     disc[i * 2], disc[i * 2 + 1]);
        }

        /* Prefer unused Audio SNK with SBC, then in-use Audio SNK with SBC. */
        {
            int want_in_use;
            for (want_in_use = 0; want_in_use <= 1 && !picked; want_in_use++) {
                for (i = 0; i < nseid && !picked; i++) {
                    int seid, in_use, tsep, media;
                    parse_sep(disc + i * 2, &seid, &in_use, &tsep, &media);
                    if (tsep != 1 || media != 0) continue; /* need Audio Sink */
                    if (fallback_seid < 0) fallback_seid = seid;
                    if (in_use != want_in_use) continue;

                    memset(&s->sink, 0, sizeof s->sink);
                    s->sink.seid = seid;
                    msg = get_caps_for_seid(s, seid, caps, (int)sizeof caps, &caps_len);
                    if (msg != AV_MSG_ACCEPT) {
                        log_line("avdtp: caps failed for SEID %d (msg=%d)", seid, msg);
                        continue;
                    }
                    log_hex_prefix("avdtp: caps raw", caps, caps_len, 48);
                    if (parse_sbc_caps(caps, caps_len, &s->sink)) {
                        if (in_use)
                            log_line("avdtp: SBC on in-use SEID %d — still taking it", seid);
                        picked = 1;
                        break;
                    }
                    log_line("avdtp: SEID %d has no SBC (will try next / fallback)", seid);
                }
            }
        }

        /* Fallback: any Audio SNK even without advertised SBC — still try SetConfig SBC */
        if (!picked && fallback_seid >= 0) {
            log_line("avdtp: no advertised SBC; fallback SetConfig on Audio SNK %d",
                     fallback_seid);
            memset(&s->sink, 0, sizeof s->sink);
            s->sink.seid = fallback_seid;
            /* Default SBC caps: 48/44.1, joint/stereo, 16 blk, 8 sb, loudness, bp 2-53 */
            s->sink.sbc_caps[0] = 0x3F; /* all rates + all channel modes */
            s->sink.sbc_caps[1] = 0xF5; /* 16/12/8/4 blocks, 8/4 sb, loudness */
            s->sink.sbc_caps[2] = 2;
            s->sink.sbc_caps[3] = 53;
            s->sink.bitpool_min = 2;
            s->sink.bitpool_max = 53;
            s->sink.have_sbc = 1; /* synthetic — we will offer SBC */
            picked = 1;
        }

        /* Last resort: GetCaps on every SEP (including SRC) looking for SBC */
        if (!picked) {
            for (i = 0; i < nseid && !picked; i++) {
                int seid, in_use, tsep, media;
                parse_sep(disc + i * 2, &seid, &in_use, &tsep, &media);
                memset(&s->sink, 0, sizeof s->sink);
                s->sink.seid = seid;
                msg = get_caps_for_seid(s, seid, caps, (int)sizeof caps, &caps_len);
                if (msg != AV_MSG_ACCEPT) continue;
                log_hex_prefix("avdtp: caps raw", caps, caps_len, 48);
                if (parse_sbc_caps(caps, caps_len, &s->sink)) {
                    log_line("avdtp: SBC found on SEP seid=%d tsep=%s (last-resort)",
                             seid, tsep ? "SNK" : "SRC");
                    picked = 1;
                }
            }
        }

        if (!picked) {
            log_line("avdtp: the headset has no SBC sink");
            return 0;
        }
    }
    log_line("avdtp: SBC sink %d, capabilities %02x %02x, bitpool %u-%u",
             s->sink.seid, s->sink.sbc_caps[0], s->sink.sbc_caps[1],
             (unsigned)s->sink.bitpool_min, (unsigned)s->sink.bitpool_max);

    if (!pick_sbc_config(&s->sink, s->sbc_cfg, &s->bitpool))
        return 0;
    memcpy(s->sink.sbc_caps, s->sbc_cfg, 4); /* store chosen */

    /* SetConfiguration: ACP SEID, INT SEID, Media Transport + Media Codec */
    {
        int n = 0;
        body[n++] = (unsigned char)(s->sink.seid << 2);
        body[n++] = (unsigned char)(s->int_seid << 2);
        body[n++] = 0x01; body[n++] = 0x00; /* Media Transport */
        body[n++] = 0x07; body[n++] = 0x06; /* Media Codec len 6 */
        body[n++] = 0x00; /* Audio << 4 */
        body[n++] = 0x00; /* SBC */
        memcpy(body + n, s->sbc_cfg, 4);
        n += 4;
        log_hex_prefix("avdtp: SET_CONFIGURATION body", body, n, 32);
        log_line("avdtp: SBC config %02x %02x bitpool %u-%u (freq %s, mode %s, "
                 "blocks %s, subbands %s, alloc %s)",
                 s->sbc_cfg[0], s->sbc_cfg[1], s->sbc_cfg[2], s->sbc_cfg[3],
                 (s->sbc_cfg[0] & 0x10) ? "48k" : (s->sbc_cfg[0] & 0x20) ? "44.1k" : "?",
                 (s->sbc_cfg[0] & 0x01) ? "joint" : (s->sbc_cfg[0] & 0x02) ? "stereo" :
                 (s->sbc_cfg[0] & 0x04) ? "dual" : "mono",
                 (s->sbc_cfg[1] & 0x10) ? "16" : (s->sbc_cfg[1] & 0x20) ? "12" :
                 (s->sbc_cfg[1] & 0x40) ? "8" : "4",
                 (s->sbc_cfg[1] & 0x04) ? "8" : "4",
                 (s->sbc_cfg[1] & 0x01) ? "loudness" : "snr");
        msg = avdtp_cmd(s, AV_SET_CONFIGURATION, body, n, rsp, (int)sizeof rsp, &rsp_len);
        if (msg != AV_MSG_ACCEPT && s->sbc_cfg[2] != s->sbc_cfg[3]) {
            /* Some sinks only take a single bitpool: retry fixed (no adaptation). */
            log_line("avdtp: bitpool range %u-%u refused — retrying with fixed bitpool %d",
                     s->sbc_cfg[2], s->sbc_cfg[3], s->bitpool);
            s->sbc_cfg[2] = s->sbc_cfg[3] = (unsigned char)s->bitpool;
            memcpy(body + n - 4, s->sbc_cfg, 4);
            msg = avdtp_cmd(s, AV_SET_CONFIGURATION, body, n, rsp, (int)sizeof rsp, &rsp_len);
        }
        if (msg != AV_MSG_ACCEPT) {
            log_line("avdtp: configuration refused");
            return 0;
        }
        s->bitpool_lo = s->sbc_cfg[2];
        s->bitpool_hi = s->sbc_cfg[3];
        s->configured = 1;
    }

    /* Open */
    body[0] = (unsigned char)(s->sink.seid << 2);
    msg = avdtp_cmd(s, AV_OPEN, body, 1, rsp, (int)sizeof rsp, &rsp_len);
    if (msg != AV_MSG_ACCEPT) {
        log_line("avdtp: open refused");
        return 0;
    }

    /* Media channel — same PSM 0x19, separate L2CAP CID (A2DP spec).
     * Close inbound SDP first so a sink's SDP discovery storm cannot fill slots or
     * steal ACL credits during media CFG; keep signaling scid open. */
    btlink_close_inbound_sdp(link);
    {
        long settle = now_ms() + 80;
        while (now_ms() < settle)
            btlink_pump(link, 20);
    }
    log_line("avdtp: opening media L2CAP on PSM %#x (signaling scid %#x kept)",
             s->psm, s->sig_scid);
    s->media_scid = btlink_chan_open(link, s->psm, 16000);
    if (!s->media_scid) {
        log_line("avdtp: media channel failed — retry once after DISC settle");
        {
            long settle = now_ms() + 150;
            while (now_ms() < settle)
                btlink_pump(link, 20);
        }
        /* Ensure signaling still open before retry. */
        if (!btlink_chan_is_open(link, s->sig_scid)) {
            log_line("avdtp: signaling died during media open — abort");
            return 0;
        }
        s->media_scid = btlink_chan_open(link, s->psm, 16000);
    }
    if (!s->media_scid) {
        log_line("avdtp: media channel failed");
        return 0;
    }
    log_line("avdtp: media channel open scid %#x (signaling scid %#x)",
             s->media_scid, s->sig_scid);

    /* Start */
    body[0] = (unsigned char)(s->sink.seid << 2);
    msg = avdtp_cmd(s, AV_START, body, 1, rsp, (int)sizeof rsp, &rsp_len);
    if (msg != AV_MSG_ACCEPT) {
        log_line("avdtp: start status %d, streaming anyway", msg);
    }
    s->streaming = 1;
    log_line("avdtp: streaming SBC %d Hz, bitpool %d",
             s->sink.sample_rate, s->bitpool);
    return 1;
}

int avdtp_send_media(avdtp_session *s, const unsigned char *sbc_frames, int len,
                     int samples_in_packet, int n_sbc_frames)
{
    unsigned char pkt[HCI_PKT_MAX];
    int n;
    uint16_t seq0;
    uint32_t ts0;

    if (!s || !s->streaming || !s->media_scid || len <= 0) return 0;
    seq0 = s->rtp_seq; ts0 = s->rtp_ts;
    n = avdtp_build_media(s, sbc_frames, len, samples_in_packet, n_sbc_frames,
                          pkt, (int)sizeof pkt);
    if (n <= 0) {
        log_line("avdtp: media frame too large (%d)", len);
        return 0;
    }
    if (!btlink_l2_send_media(s->link, s->media_scid, pkt, n)) {
        s->rtp_seq = seq0; s->rtp_ts = ts0;   /* not sent: keep RTP continuous */
        return 0;
    }
    avdtp_dump_packet(s, pkt, n);
    return 1;
}

void avdtp_teardown(avdtp_session *s)
{
    unsigned char body[1], rsp[64];
    int rsp_len = 0;

    if (!s || !s->link) return;
    avdtp_dump_close(s);
    btlink_set_inbound_rx(s->link, BTLINK_PSM_AVDTP, NULL, NULL);
    if (s->streaming && s->sig_scid && !s->remote_closed) {
        s->quick = 1;   /* shutting down: one short try */
        body[0] = (unsigned char)(s->sink.seid << 2);
        (void)avdtp_cmd(s, AV_CLOSE, body, 1, rsp, (int)sizeof rsp, &rsp_len);
        s->streaming = 0;
    }
    if (s->media_scid) btlink_chan_close(s->link, s->media_scid);
    if (s->sig_scid) btlink_chan_close(s->link, s->sig_scid);
    s->media_scid = s->sig_scid = 0;
}
