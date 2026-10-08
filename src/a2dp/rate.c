/* Developed by X-F1REBALL-X. */
#include "rate.h"

#define DOWN_GAP_MS   300   /* at most one step down per this */
#define UP_CALM_MS   6000   /* queue near empty this long before stepping up */
#define UP_GAP_MS    3000   /* and at most one step up per this */

int hb_frames_per_packet(int mtu, int frame_len)
{
    int n;
    if (frame_len <= 0) return 1;
    n = (mtu - HB_MEDIA_HDR) / frame_len;
    if (n > 15) n = 15;
    if (n < 1) n = 1;
    return n;
}

int hb_media_queue_cap(int pkt_ms, int target_ms)
{
    int n;
    if (pkt_ms < 1) pkt_ms = 1;
    if (target_ms < 0) target_ms = 0;
    n = (target_ms + pkt_ms / 2) / pkt_ms;
    return n < HB_QUEUE_MIN_PKTS ? HB_QUEUE_MIN_PKTS : n;
}

void hb_rate_init(hb_rate *r, int lo, int hi, int start, long now_ms)
{
    r->lo = lo > HB_RATE_FLOOR ? lo : HB_RATE_FLOOR;
    r->hi = hi < HB_RATE_CEIL ? hi : HB_RATE_CEIL;
    if (r->lo > hi) r->lo = hi;           /* range above the floor: stay inside it */
    if (r->hi < lo) r->hi = lo;
    if (r->lo > r->hi) r->lo = r->hi;
    r->cur = start < r->lo ? r->lo : start > r->hi ? r->hi : start;
    r->t_down = r->t_up = -100000;
    r->t_calm = now_ms;
    r->last_drops = 0;
    r->downs = r->ups = 0;
}

int hb_rate_update(hb_rate *r, long now, int queue, int qmax, long drops)
{
    int dropped = drops > r->last_drops, step = 0;

    r->last_drops = drops;
    if (qmax < 1) qmax = 1;
    if (dropped || queue * 2 >= qmax) step = dropped ? -4 : -3;   /* falling behind */
    else if (queue * 4 >= qmax) step = -1;                        /* getting tight */

    if (step < 0) {
        r->t_calm = now;
        if (now - r->t_down >= DOWN_GAP_MS && r->cur > r->lo) {
            r->cur += step;
            if (r->cur < r->lo) r->cur = r->lo;
            r->t_down = now;
            r->downs++;
        }
        return r->cur;
    }
    if (queue > 1) {                      /* not calm yet */
        r->t_calm = now;
        return r->cur;
    }
    if (now - r->t_calm >= UP_CALM_MS && now - r->t_up >= UP_GAP_MS &&
        now - r->t_down >= UP_CALM_MS && r->cur < r->hi) {
        r->cur++;
        r->t_up = now;
        r->ups++;
    }
    return r->cur;
}
