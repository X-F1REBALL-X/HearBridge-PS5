/* Developed by X-F1REBALL-X. L2CAP config pacing against a fake headset
 * whose replies take a while (MediaTek link, 1.5 s like the realme Buds
 * T300 log) or come at once (Marvell). On MediaTek our CFG_REQ must not be
 * repeated with a new ident (the headset takes that as a re-config of an
 * open channel), a lost CFG_RSP must not stall the channel, and a re-config
 * the headset starts on the open media channel gets our CFG_REQ and then a
 * SUSPEND/START cue. Marvell keeps its 1 s resend. */
#include "btlink.h"
#include "hci_cmd.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char lastlog[64][200];
static int nlog;
void log_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lastlog[nlog % 64], sizeof lastlog[0], fmt, ap);
    va_end(ap);
    nlog++;
}
static int logged(const char *needle)
{
    int i;
    for (i = 0; i < 64 && i < nlog; i++) if (strstr(lastlog[i], needle)) return 1;
    return 0;
}

#define HANDLE 0x033
static unsigned short g_handle = HANDLE;

/* Fake headset. */
typedef struct { long due; int n; unsigned char d[64]; } qpkt;
static qpkt aclq[64]; static int naclq;
static unsigned char evq[64][16]; static int evlen[64], evh, evt;
static long rtt;                 /* reply delay */
static int lose_cfg_rsp;         /* the next CFG_RSP is read by "the system" */
static int ignore_cfg;           /* the next CFG_REQ never reaches the headset */
static int peer_reconfigs;       /* CFG_REQ with a new ident on an open channel */
static int our_cfg_reqs, our_new_ids;
static struct { unsigned our_cid, peer_cid; int got_cfg, sent_cfg, open; int last_id; } pc[8];
static int npc;
static unsigned char peer_id = 0x20;

static void ev_push(const unsigned char *e, int n) { memcpy(evq[evt % 64], e, (size_t)n); evlen[evt % 64] = n; evt++; }
static unsigned get16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

static void peer_sig(unsigned char code, unsigned char id, const unsigned char *b, int blen)
{
    qpkt *q;
    if (naclq >= 64) return;
    q = &aclq[naclq++];
    q->due = now_ms() + rtt;
    put16(q->d, g_handle | 0x2000);
    put16(q->d + 2, (unsigned)(4 + 4 + blen));
    put16(q->d + 4, (unsigned)(4 + blen));
    put16(q->d + 6, 1);
    q->d[8] = code; q->d[9] = id; put16(q->d + 10, (unsigned)blen);
    memcpy(q->d + 12, b, (size_t)blen);
    q->n = 12 + blen;
}
static void peer_cfg_req(int i)
{
    unsigned char b[8];
    put16(b, pc[i].our_cid); put16(b + 2, 0); b[4] = 1; b[5] = 2; put16(b + 6, 679);
    peer_sig(0x04, peer_id++, b, 8);
    if (pc[i].open) pc[i].open = pc[i].got_cfg = 0;   /* its own re-config: waits for ours */
    pc[i].sent_cfg = 1;
}

static int f_send(void *s, const unsigned char *f, int n)
{
    unsigned char e[7] = { 0x13, 5, 1 };
    (void)s;
    put16(e + 3, g_handle); put16(e + 5, 1);
    ev_push(e, 7);                                   /* completed at once */
    if (n < 12 || get16(f + 6) != 1) return 1;       /* data, not signalling */
    {
        unsigned char code = f[8], id = f[9];
        const unsigned char *b = f + 12;
        int i;
        if (code == 0x0A) {                          /* INFO_REQ -> not supported */
            unsigned char r[4]; put16(r, get16(b)); put16(r + 2, 1);
            peer_sig(0x0B, id, r, 4);
        } else if (code == 0x02) {                   /* CONN_REQ */
            unsigned char r[8];
            i = npc++;
            pc[i].our_cid = get16(b + 2); pc[i].peer_cid = 0x40 + (unsigned)i; pc[i].last_id = -1;
            put16(r, pc[i].peer_cid); put16(r + 2, pc[i].our_cid); put16(r + 4, 0); put16(r + 6, 0);
            peer_sig(0x03, id, r, 8);
        } else if (code == 0x04) {                   /* CFG_REQ */
            unsigned char r[6];
            for (i = 0; i < npc; i++) if (pc[i].peer_cid == get16(b)) break;
            if (i == npc) return 1;
            our_cfg_reqs++;
            if (ignore_cfg) { ignore_cfg--; return 1; }
            if (id != pc[i].last_id) {
                our_new_ids++;
                if (pc[i].open) {                    /* new ident on an open channel */
                    peer_reconfigs++;
                    pc[i].open = 0; pc[i].sent_cfg = 0;
                }
            }
            pc[i].last_id = id;
            put16(r, pc[i].our_cid); put16(r + 2, 0); put16(r + 4, 0);
            if (lose_cfg_rsp) lose_cfg_rsp--;
            else peer_sig(0x05, id, r, 6);
            pc[i].got_cfg = 1;
            if (!pc[i].sent_cfg) peer_cfg_req(i);
            if (pc[i].got_cfg && pc[i].sent_cfg) pc[i].open = 1;
        }
    }
    return 1;
}
static int f_acl(void *s, unsigned char *d, int cap)
{
    int i, best = -1, n;
    long now = now_ms();
    (void)s;
    for (i = 0; i < naclq; i++)
        if (aclq[i].due <= now && (best < 0 || aclq[i].due < aclq[best].due)) best = i;
    if (best < 0) return 0;
    n = aclq[best].n < cap ? aclq[best].n : cap;
    memcpy(d, aclq[best].d, (size_t)n);
    aclq[best] = aclq[--naclq];
    return n;
}
static int f_event(void *s, unsigned char *d, int cap)
{
    int n;
    (void)s;
    if (evh == evt) return 0;
    n = evlen[evh % 64] < cap ? evlen[evh % 64] : cap;
    memcpy(d, evq[evh % 64], (size_t)n); evh++;
    return n;
}
static int f_pump(void *s, int ms) { (void)s; if (ms > 0) usleep((useconds_t)(ms > 5 ? 5 : ms) * 1000); return 1; }
static int f_cmd(void *s, unsigned op, const void *a, int n) { (void)s; (void)op; (void)a; (void)n; return 1; }
static const hci_ops OPS = { f_acl, f_event, f_pump, f_cmd, f_send, NULL, NULL };

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

static btlink *fresh(int mtk, long delay)
{
    hci_t hci = { NULL, &OPS };
    unsigned char addr[6] = { 0xA2, 0xF9, 0x68, 0xF2, 0xA3, 0xB0 }, key[16] = { 1 };
    btlink *l = btlink_create(hci, 1021, 8);
    hci_set_mediatek(mtk);
    rtt = delay; lose_cfg_rsp = ignore_cfg = 0; peer_reconfigs = our_cfg_reqs = our_new_ids = 0;
    npc = 0; naclq = 0; memset(pc, 0, sizeof pc);
    btlink_adopt(l, HANDLE, addr, key, 4, "realme Buds T300");
    return l;
}
static void run(btlink *l, long ms) { long end = now_ms() + ms; while (now_ms() < end) btlink_pump(l, 5); }

int main(void)
{
    btlink *l;
    unsigned sig, media;
    int reqs;

    /* Marvell: quick replies, one CFG_REQ each, nothing changes. */
    l = fresh(0, 20);
    sig = btlink_chan_open(l, BTLINK_PSM_AVDTP, 8000);
    media = btlink_chan_open(l, BTLINK_PSM_AVDTP, 16000);
    CHECK(sig && media, "marvell: signalling and media channels open");
    CHECK(our_cfg_reqs == 2 && peer_reconfigs == 0, "marvell: one CFG_REQ per channel, no re-config");
    btlink_destroy(l);

    /* Marvell resend stays 1 s with a new ident (unchanged): slow peer. */
    l = fresh(0, 1500);
    sig = btlink_chan_open(l, BTLINK_PSM_AVDTP, 8000);
    CHECK(sig && our_new_ids >= 2, "marvell: slow reply still re-sent after 1 s with a new ident (old behaviour kept)");
    btlink_destroy(l);

    /* MediaTek, 1.5 s replies (the T300 log): no second ident, no re-config. */
    l = fresh(1, 1500);
    sig = btlink_chan_open(l, BTLINK_PSM_AVDTP, 8000);
    media = btlink_chan_open(l, BTLINK_PSM_AVDTP, 16000);
    CHECK(sig && media, "mediatek slow: both channels open");
    CHECK(our_new_ids == 2, "mediatek slow: one ident per channel (no new-ident resend)");
    CHECK(peer_reconfigs == 0, "mediatek slow: the headset never sees a re-config");
    run(l, 600);
    CHECK(!btlink_media_rebind_due(l, media), "mediatek slow: no SUSPEND/START needed");
    btlink_destroy(l);

    /* MediaTek, our CFG_RSP read by the system stack: ours is taken as
     * accepted once the peer configured, the channel opens, no re-config. */
    l = fresh(1, 1500);
    sig = btlink_chan_open(l, BTLINK_PSM_AVDTP, 8000);
    lose_cfg_rsp = 1;
    nlog = 0;
    media = btlink_chan_open(l, BTLINK_PSM_AVDTP, 16000);
    CHECK(media && btlink_chan_is_open(l, media), "mediatek lost CFG_RSP: media channel still opens");
    CHECK(peer_reconfigs == 0 && our_new_ids == 2, "mediatek lost CFG_RSP: no new ident, no re-config");
    CHECK(logged("taking ours as accepted"), "mediatek lost CFG_RSP: logged as assumed");
    btlink_destroy(l);

    /* MediaTek, the first CFG_REQ is lost (no reply, no peer CFG_REQ):
     * resent after 4 s with the same ident and MTU. */
    l = fresh(1, 20);
    sig = btlink_chan_open(l, BTLINK_PSM_AVDTP, 8000);
    reqs = our_cfg_reqs;
    ignore_cfg = 1;
    nlog = 0;
    {
        long t0 = now_ms(), dt;
        media = btlink_chan_open(l, BTLINK_PSM_AVDTP, 16000);
        dt = now_ms() - t0;
        printf("info media open after %ld ms with a lost first CFG_REQ\n", dt);
        CHECK(media && dt >= 3900 && dt < 6000, "mediatek lost CFG_REQ: media opens after the 4 s resend");
    }
    CHECK(our_cfg_reqs == reqs + 2 && logged("(resend, same id)"), "mediatek lost CFG_REQ: one resend, same ident");
    CHECK(peer_reconfigs == 0, "mediatek lost CFG_REQ: no re-config");
    btlink_destroy(l);

    /* MediaTek, the headset re-configures the open media channel by itself:
     * we send our CFG_REQ once, and SUSPEND/START is due after it settles. */
    l = fresh(1, 100);
    sig = btlink_chan_open(l, BTLINK_PSM_AVDTP, 8000);
    media = btlink_chan_open(l, BTLINK_PSM_AVDTP, 16000);
    CHECK(sig && media, "mediatek re-config: channels open");
    run(l, 200);
    reqs = our_cfg_reqs;
    peer_cfg_req(1);                                     /* headset starts a re-config */
    run(l, 50);
    CHECK(!btlink_media_rebind_due(l, media), "mediatek re-config: not due while it is in progress");
    run(l, 700);
    CHECK(our_cfg_reqs == reqs + 1, "mediatek re-config: we sent our CFG_REQ once");
    CHECK(btlink_chan_is_open(l, media), "mediatek re-config: media channel stays open for the stream");
    CHECK(btlink_media_rebind_due(l, media), "mediatek re-config: SUSPEND/START due after it settled");
    CHECK(!btlink_media_rebind_due(l, media), "mediatek re-config: only once");
    btlink_destroy(l);

    /* MediaTek, the T300 case: the headset re-configures right after our
     * CFG_REQ that it has not answered yet: that is its reply, we send no
     * further CFG_REQ (no ping-pong). */
    l = fresh(1, 100);
    sig = btlink_chan_open(l, BTLINK_PSM_AVDTP, 8000);
    media = btlink_chan_open(l, BTLINK_PSM_AVDTP, 16000);
    run(l, 200);
    reqs = our_cfg_reqs;
    peer_cfg_req(1);                                     /* first round: we answer with ours */
    lose_cfg_rsp = 1;                                    /* its reply to ours goes missing */
    run(l, 300);
    peer_cfg_req(1);                                     /* and it sends another CFG_REQ */
    run(l, 300);
    CHECK(our_cfg_reqs == reqs + 1, "mediatek re-config while ours is unanswered: no extra CFG_REQ");
    run(l, 4500);
    CHECK(btlink_chan_is_open(l, media) && btlink_media_rebind_due(l, media),
          "mediatek re-config, reply lost: taken as done after 4 s, SUSPEND/START due");
    btlink_destroy(l);

    /* Marvell, same headset re-config: answered as before, nothing else. */
    l = fresh(0, 20);
    sig = btlink_chan_open(l, BTLINK_PSM_AVDTP, 8000);
    media = btlink_chan_open(l, BTLINK_PSM_AVDTP, 16000);
    reqs = our_cfg_reqs;
    peer_cfg_req(1);
    run(l, 800);
    CHECK(our_cfg_reqs == reqs && !btlink_media_rebind_due(l, media) && btlink_chan_is_open(l, media),
          "marvell re-config: answered only, no CFG_REQ, no SUSPEND/START");
    {
        unsigned char e[4] = { 0x11, 2 };
        put16(e + 2, HANDLE);
        nlog = 0;
        ev_push(e, 4);
        run(l, 20);
        CHECK(logged("Flush Occurred"), "Flush Occurred on our handle is logged");
    }
    btlink_destroy(l);

    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
