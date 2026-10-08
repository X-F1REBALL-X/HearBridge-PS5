/* Developed by X-F1REBALL-X. Media pacing: a backlog after a radio stall
 * drains back to the target quickly (no lasting latency), and at the
 * normal rate nothing queues or drops. */
#include "acl_pool.h"
#include "rate.h"
#include <stdio.h>

static int run(const char *name, long stall_at, long stall_ms, int qcap, int *max_after, long *drain_ms)
{
    long clock = 0, now, dur = 21;           /* 21.33 ms packets, truncated */
    double pkt_ms = 1024 * 1000.0 / 48000.0, prod = 0;
    long q_due[256]; int qn = 0, drops = 0, maxq_tail = 0;
    long radio_free = 0, drained_at = -1;
    for (now = 0; now < 6000; now++) {
        while (prod <= now) {
            if (qn == qcap) { int i; drops++; for (i = 1; i < qn; i++) q_due[i - 1] = q_due[i]; qn--; }
            q_due[qn] = acl_pace_due(&clock, dur, now, qn);
            qn++;
            prod += pkt_ms;
        }
        int stalled = stall_ms && now >= stall_at && now < stall_at + stall_ms;
        /* radio: one packet per 3 ms when not stalled (plenty of headroom) */
        while (!stalled && qn && radio_free <= now &&
               (q_due[0] <= now || qn > PACE_TARGET_PKTS)) {
            int i; for (i = 1; i < qn; i++) q_due[i - 1] = q_due[i]; qn--;
            radio_free = now + 3;
        }
        if (stall_ms && now >= stall_at + stall_ms) {
            if (drained_at < 0 && qn <= PACE_TARGET_PKTS) drained_at = now;
            if (now > stall_at + stall_ms + 1000 && qn > maxq_tail) maxq_tail = qn;
        } else if (!stall_ms && now > 500 && qn > maxq_tail) maxq_tail = qn;
    }
    *max_after = maxq_tail;
    *drain_ms = drained_at < 0 ? -1 : drained_at - (stall_at + stall_ms);
    printf("     %s: drops=%d, queue 1 s after=%d, drained in %ld ms\n", name, drops, maxq_tail, *drain_ms);
    return drops;
}

int main(void)
{
    int fails = 0, mq; long dm;
    int qlow = hb_media_queue_cap(21, HB_QUEUE_LOW_MS), qstable = hb_media_queue_cap(21, HB_QUEUE_STABLE_MS), d;
    if (run("normal rate", 0, 0, 256, &mq, &dm) || mq > PACE_TARGET_PKTS) { puts("FAIL normal rate queues/drops"); fails++; }
    if (run("400 ms stall", 2000, 400, 256, &mq, &dm) || mq > PACE_TARGET_PKTS || dm < 0 || dm > 300) {
        puts("FAIL backlog after a stall did not drain"); fails++;
    }
    /* Same with the real queue caps: low latency drops the oldest audio
     * beyond ~200 ms during a long stall (by design), stable rides it out. */
    if (run("normal rate, low-latency queue", 0, 0, qlow, &mq, &dm) || mq > PACE_TARGET_PKTS) {
        puts("FAIL low-latency queue: drops at the normal rate"); fails++;
    }
    d = run("400 ms stall, low-latency queue", 2000, 400, qlow, &mq, &dm);
    if (d > 400 / 21 - qlow + 2 || mq > PACE_TARGET_PKTS || dm < 0 || dm > 300) {
        puts("FAIL low-latency queue: stall drops more than the overflow or does not drain"); fails++;
    }
    if (run("400 ms stall, stable queue", 2000, 400, qstable, &mq, &dm) || mq > PACE_TARGET_PKTS || dm < 0 || dm > 300) {
        puts("FAIL stable queue: drops in a 400 ms stall"); fails++;
    }
    puts(fails ? "FAIL pacing" : "ok   pacing: no drops at normal rate, stall backlog drains to target");
    return fails != 0;
}
