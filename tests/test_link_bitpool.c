/* Developed by X-F1REBALL-X.
 * The console report behind this test (Xbox headset, fw 10.20): a clean
 * link (0 drops, backlog 0 at every 1 s sample, ACL completion gaps of
 * ~24 ms with peaks of ~96 ms), sink bitpool range 2-60, yet the bitpool
 * sat at the floor (22) for 10+ minutes.
 *
 * Model of the real send path: the stream loop encodes 1024-frame chunks
 * and may run 40 ms ahead of the audio clock; the packer fills packets up
 * to the 672-byte MTU; btlink queues them with the media pacer
 * (acl_pace_due, up to PACE_TARGET_PKTS held until due) and a queue cap
 * from hb_media_queue_cap() (never below 8); the controller has 2 ACL
 * buffers whose completions come back in bursts every 24 ms, with a 96 ms
 * gap every 2 s. The radio itself has room for far more than bitpool 53.
 *
 * Paced packets sitting in the queue were counted as congestion: with the
 * ~200 ms queue (8 packets) two queued packets already meant "getting
 * tight" and step-up needed the queue at <= 1 for 6 s. The legacy rule is
 * kept here to show the scenario reproduces it. */
#include "acl_pool.h"
#include "rate.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

static int frame_len(int bp, int dual)
{
    /* 16 blocks, 8 subbands: joint stereo or dual channel (A2DP 12.9) */
    return dual ? 4 + 8 + (16 * 2 * bp + 7) / 8 : 4 + 8 + (8 + 16 * bp + 7) / 8;
}

/* The controller before this fix (rate.c up to 1.0.2 + queue cap). */
static int legacy_update(hb_rate *r, long now, int queue, int qmax, long drops)
{
    int dropped = drops > r->last_drops, step = 0;
    r->last_drops = drops;
    if (qmax < 1) qmax = 1;
    if (dropped || queue * 2 >= qmax) step = dropped ? -4 : -3;
    else if (queue * 4 >= qmax) step = -1;
    if (step < 0) {
        r->t_calm = now;
        if (now - r->t_down >= 300 && r->cur > r->lo) {
            r->cur += step;
            if (r->cur < r->lo) r->cur = r->lo;
            r->t_down = now;
        }
        return r->cur;
    }
    if (queue > 1) { r->t_calm = now; return r->cur; }
    if (now - r->t_calm >= 6000 && now - r->t_up >= 3000 && now - r->t_down >= 6000 && r->cur < r->hi) {
        r->cur++;
        r->t_up = now;
    }
    return r->cur;
}

typedef struct { int bp_end, bp_min, bp_max, max_q; long drops, sent; double secs_at_top; } result;

/* radio_kbps: air capacity for media; target_ms: media queue target;
 * start: first bitpool; ceil: controller ceiling; legacy: old rule. */
static int g_limit = 2;      /* ACL buffers in the controller */
static result run(int radio_kbps, int target_ms, int start, int ceil, int legacy, int dual, int secs)
{
    enum { QMAX = 64 };
    hb_rate r;
    result o;
    long q_due[QMAX], q_bytes[QMAX], now, clock = 0, drops = 0, t_start = 0, samples = 0;
    long air_free = 0, done_unreported = 0, next_burst = 24;
    int qn = 0, outstanding = 0, limit = g_limit, cap = 8, per_pkt, pending = 0, fsz, mtu = 672;
    long inflight_done[16]; int nin = 0;

    memset(&o, 0, sizeof o);
    hb_rate_init(&r, 2, 60, start, 0);
    hb_rate_set_ceiling(&r, 60, ceil);
    o.bp_min = o.bp_max = r.cur;
    fsz = frame_len(r.cur, dual);
    per_pkt = (mtu - HB_MEDIA_HDR) / fsz; if (per_pkt > 15) per_pkt = 15;
    for (now = 0; now < secs * 1000L; now++) {
        /* producer: 1024-frame chunks, at most 40 ms ahead of the clock */
        while (samples * 1000L / 48000 - (now - t_start) <= 40) {
            int f;
            for (f = 0; f < 1024 / 128; f++) {
                if (++pending < per_pkt) continue;
                pending = 0;
                if (qn >= cap || qn >= QMAX) {         /* drop the oldest media packet */
                    memmove(q_due, q_due + 1, sizeof q_due[0] * (size_t)(qn - 1));
                    memmove(q_bytes, q_bytes + 1, sizeof q_bytes[0] * (size_t)(qn - 1));
                    qn--; drops++;
                }
                {
                    long pkt_ms = (long)per_pkt * 128 * 1000 / 48000;
                    q_due[qn] = acl_pace_due(&clock, pkt_ms, now, qn);
                    q_bytes[qn] = HB_MEDIA_HDR + per_pkt * fsz + 8;
                    qn++;
                }
                if (qn > o.max_q) o.max_q = qn;
                {
                    int bp = legacy ? legacy_update(&r, now, qn, cap, drops)
                                    : hb_rate_update(&r, now, qn, cap, drops);
                    if (bp < o.bp_min) o.bp_min = bp;
                    if (bp > o.bp_max) o.bp_max = bp;
                    fsz = frame_len(bp, dual);
                    per_pkt = (mtu - HB_MEDIA_HDR) / fsz; if (per_pkt > 15) per_pkt = 15;
                }
            }
            samples += 1024;
        }
        /* completions come back in bursts: every 24 ms, a 96 ms gap every 2 s */
        if (now >= next_burst) {
            int i, k = 0;
            for (i = 0; i < nin; i++) if (inflight_done[i] <= now) k++;
            for (i = 0; i + k < nin; i++) inflight_done[i] = inflight_done[i + k];
            nin -= k; outstanding -= k; done_unreported = 0;
            next_burst = now + ((now / 2000) != ((now + 24) / 2000) ? 96 : 24);
        }
        /* sender: head of the queue when due (or backlog above target) and a buffer is free */
        while (qn && outstanding < limit && (q_due[0] <= now || qn > PACE_TARGET_PKTS)) {
            long air = q_bytes[0] * 8 / radio_kbps;          /* ms on air */
            if (air_free < now) air_free = now;
            air_free += air > 0 ? air : 1;
            inflight_done[nin++] = air_free;
            outstanding++;
            memmove(q_due, q_due + 1, sizeof q_due[0] * (size_t)(qn - 1));
            memmove(q_bytes, q_bytes + 1, sizeof q_bytes[0] * (size_t)(qn - 1));
            qn--; o.sent++;
        }
        /* once a second: queue cap from the target (as tune_link, btlink clamps to >= 8) */
        if (now % 1000 == 999) {
            int pkt_ms = per_pkt * 128 * 1000 / 48000;
            cap = hb_media_queue_cap(pkt_ms, target_ms);
            if (cap < 8) cap = 8;
            if (cap > QMAX) cap = QMAX;
        }
        if (r.cur == r.hi) o.secs_at_top += 0.001;
    }
    (void)done_unreported;
    o.bp_end = r.cur;
    o.drops = drops;
    return o;
}

int main(void)
{
    result o;
    char m[200];

    o = run(1500, HB_QUEUE_LOW_MS, 35, HB_RATE_CEIL, 1, 0, 120);
    snprintf(m, sizeof m, "reproduced: legacy rule, clean bursty link, ~200 ms queue -> bitpool %d (min %d), %ld drops",
             o.bp_end, o.bp_min, o.drops);
    CHECK(o.bp_end <= 24 && o.drops == 0, m);

    o = run(1500, HB_QUEUE_LOW_MS, 35, HB_RATE_CEIL, 0, 0, 120);
    snprintf(m, sizeof m, "fixed: same link, low latency -> bitpool %d (min %d), %ld drops, max queue %d",
             o.bp_end, o.bp_min, o.drops, o.max_q);
    CHECK(o.bp_end >= 51 && o.drops == 0, m);

    o = run(1500, HB_QUEUE_LOW_MS, 22, HB_RATE_CEIL, 0, 0, 90);
    snprintf(m, sizeof m, "fixed: from the floor (22) it climbs to %d within 90 s, %ld drops", o.bp_end, o.drops);
    CHECK(o.bp_end >= 51 && o.drops == 0, m);

    o = run(1500, HB_QUEUE_STABLE_MS, 35, HB_RATE_CEIL, 0, 0, 120);
    snprintf(m, sizeof m, "fixed: stable mode -> bitpool %d, %ld drops", o.bp_end, o.drops);
    CHECK(o.bp_end >= 51 && o.drops == 0, m);

    o = run(1500, HB_QUEUE_LOW_MS, 35, HB_RATE_CEIL, 0, 0, 120);
    CHECK(o.bp_max <= HB_RATE_CEIL, "standard mode never goes above 53");

    /* A link that cannot carry bitpool 53: settles lower, few drops. */
    o = run(300, HB_QUEUE_LOW_MS, 35, HB_RATE_CEIL, 0, 0, 180);
    snprintf(m, sizeof m, "slow radio (300 kbit/s air): settles at %d (range %d-%d), %ld drops",
             o.bp_end, o.bp_min, o.bp_max, o.drops);
    CHECK(o.bp_end < 53 && o.bp_end >= HB_RATE_FLOOR && o.drops <= 20, m);

    g_limit = 8;
    o = run(1500, HB_QUEUE_LOW_MS, 22, HB_RATE_CEIL, 0, 0, 90);
    snprintf(m, sizeof m, "8 ACL buffers: from 22 to %d, %ld drops", o.bp_end, o.drops);
    CHECK(o.bp_end >= 51 && o.drops == 0, m);

    o = run(1500, HB_QUEUE_LOW_MS, 35, 60, 0, 0, 180);
    snprintf(m, sizeof m, "high-quality ceiling 60: reaches %d, %ld drops", o.bp_end, o.drops);
    CHECK(o.bp_end == 60 && o.drops == 0, m);

    /* SBC-XQ (dual channel): ~2x the bytes per bitpool step. */
    o = run(1500, HB_QUEUE_LOW_MS, 35, 47, 0, 1, 120);
    snprintf(m, sizeof m, "dual channel (XQ) ceiling 47 on a clean link: reaches %d, %ld drops", o.bp_end, o.drops);
    CHECK(o.bp_end == 47 && o.drops == 0, m);

    g_limit = 2;
    o = run(1500, HB_QUEUE_LOW_MS, 35, 47, 0, 1, 120);
    snprintf(m, sizeof m, "dual channel on 2 ACL buffers (too slow for XQ): steps down to %d, %ld drops", o.bp_end, o.drops);
    CHECK(o.bp_end < 47 && o.drops <= 20, m);

    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
