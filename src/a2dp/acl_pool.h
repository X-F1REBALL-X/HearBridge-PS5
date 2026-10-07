/* Developed by X-F1REBALL-X.
 * ACL credit accounting and media pacing. Pure logic (time is passed in),
 * so the host test can simulate a link. */
#ifndef HB_ACL_POOL_H
#define HB_ACL_POOL_H

#define FB_IDLE_MS       120   /* no NOCP ever seen: refill after this long */
#define FB_IDLE_SEEN_MS  400   /* NOCP events do arrive: only refill after a long gap */
#define FB_STEP_MS       10    /* one credit per step in fallback */

typedef struct {
    int limit, outstanding;
    long last_credit_ms, last_fb_ms, last_stats_ms;
    unsigned long sent, queued, dropped, cred_evt, cred_fb, max_q, media_dropped;
    long gap_avg;             /* typical ms between completion events while busy (x1) */
    unsigned long stalls;     /* times the timed refill had to step in */
    long longest_gap;         /* longest wait for a completion (ms) */
} acl_pool;

/* How long to wait for a completion before the timed refill steps in:
 * a few times this link's own typical gap, never more than FB_IDLE_SEEN_MS
 * and never below FB_IDLE_MIN_MS. */
#define FB_IDLE_MIN_MS 60
long acl_pool_idle_limit(const acl_pool *p);

/* n packets completed. Real events always free credits. */
void acl_pool_credit(acl_pool *p, int n, int from_event, long now);
/* Timed refill, only when no completion arrived for a long time. */
void acl_pool_fallback(acl_pool *p, long now);
/* A packet went to the controller. */
void acl_pool_sent(acl_pool *p, long now);

/* Media pacing clock: returns the due time of the next packet and
 * advances the clock by dur_ms. A clock in the past (backlog) is
 * clamped to at most 250 ms behind, so catch-up goes at credit speed. */
long acl_pace_next(long *clock, long dur_ms, long now);
/* Media pacing target: above this many queued media packets, pacing does
 * not hold packets back (the backlog drains as fast as credits allow). */
#define PACE_TARGET_PKTS 3
#define PACE_CATCHUP_MS  60
long acl_pace_due(long *clock, long dur_ms, long now, int backlog);

#endif
