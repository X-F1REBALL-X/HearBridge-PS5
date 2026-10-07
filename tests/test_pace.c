/* Developed by X-F1REBALL-X. Media pacing: a backlog after a radio stall
 * drains back to the target quickly (no lasting latency), and at the
 * normal rate nothing queues or drops. */
#include "acl_pool.h"
#include <stdio.h>

static int run(const char *name, long stall_at, long stall_ms, int *max_after, long *drain_ms)
{
    long clock = 0, now, dur = 21;           /* 21.33 ms packets, truncated */
    double pkt_ms = 1024 * 1000.0 / 48000.0, prod = 0;
    long q_due[256]; int qn = 0, drops = 0, maxq_tail = 0;
    long radio_free = 0, drained_at = -1;
    for (now = 0; now < 6000; now++) {
        while (prod <= now) {
            if (qn == 256) { drops++; qn--; }
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
    if (run("normal rate", 0, 0, &mq, &dm) || mq > PACE_TARGET_PKTS) { puts("FAIL normal rate queues/drops"); fails++; }
    if (run("400 ms stall", 2000, 400, &mq, &dm) || mq > PACE_TARGET_PKTS || dm < 0 || dm > 300) {
        puts("FAIL backlog after a stall did not drain"); fails++;
    }
    puts(fails ? "FAIL pacing" : "ok   pacing: no drops at normal rate, stall backlog drains to target");
    return fails != 0;
}
