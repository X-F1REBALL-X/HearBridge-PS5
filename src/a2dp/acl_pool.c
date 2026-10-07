/* Developed by X-F1REBALL-X. */
#include "acl_pool.h"

void acl_pool_credit(acl_pool *p, int n, int from_event, long now)
{
    if (n <= 0) return;
    if (n > p->outstanding) n = p->outstanding;
    p->outstanding -= n;
    if (from_event) {
        long gap = now - p->last_credit_ms;
        if (gap > p->longest_gap) p->longest_gap = gap;
        if (gap >= 0 && gap < 2000)              /* learn this link's rhythm */
            p->gap_avg = p->gap_avg ? (p->gap_avg * 7 + gap) / 8 : gap;
        p->cred_evt += (unsigned long)n;
        p->last_credit_ms = now;
    } else {
        p->cred_fb += (unsigned long)n;
    }
}

long acl_pool_idle_limit(const acl_pool *p)
{
    long lim;
    if (!p->cred_evt) return FB_IDLE_MS;
    lim = p->gap_avg * 2 + 30;
    if (lim < FB_IDLE_MIN_MS) lim = FB_IDLE_MIN_MS;
    if (lim > FB_IDLE_SEEN_MS) lim = FB_IDLE_SEEN_MS;
    return lim;
}

void acl_pool_fallback(acl_pool *p, long now)
{
    if (p->outstanding <= 0) return;
    if (now - p->last_credit_ms < acl_pool_idle_limit(p)) return;
    if (now - p->last_fb_ms < FB_STEP_MS) return;
    if (now - p->last_fb_ms > FB_STEP_MS * 8) p->stalls++;   /* a new stall */
    p->last_fb_ms = now;
    acl_pool_credit(p, 1, 0, now);
}

void acl_pool_sent(acl_pool *p, long now)
{
    if (p->outstanding == 0) p->last_credit_ms = now;
    p->outstanding++;
    p->sent++;
}

long acl_pace_due(long *clock, long dur_ms, long now, int backlog)
{
    if (backlog > PACE_TARGET_PKTS) {
        /* Above target: no pacing wait, send as credits allow, so audio
         * never sits in our queue (latency) after a stall. */
        if (*clock < now) *clock = now;
        *clock += dur_ms > 0 ? dur_ms : 0;
        return now;
    }
    return acl_pace_next(clock, dur_ms, now);
}

long acl_pace_next(long *clock, long dur_ms, long now)
{
    long due;
    if (*clock < now - PACE_CATCHUP_MS) *clock = now - PACE_CATCHUP_MS;
    due = *clock;
    *clock += dur_ms > 0 ? dur_ms : 0;
    return due;
}
