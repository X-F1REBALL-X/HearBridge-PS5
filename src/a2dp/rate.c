/* Developed by X-F1REBALL-X. */
#include "rate.h"

#define DOWN_GAP_MS   300   /* at most one step down per this */
#define UP_CALM_MS   4000   /* no late packet this long before stepping up */
#define UP_GAP_MS    2000   /* and at most one step up per this */
#define CAP_HOLD_MS 30000   /* a bitpool that congested the link: retry after this */
#define LATE_CALM_MS  200   /* late packets this long: not calm (no step up) */
#define LATE_MS       600   /* late packets this long: -1 */
#define LATE_BIG_MS   300   /* half the room late this long: -3 */

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
    r->cap = 0;
    r->t_cap = -100000;
    r->t_late = -1;
    r->hold_until = -100000;
}

void hb_rate_set_ceiling(hb_rate *r, int cfg_hi, int ceil)
{
    int hi = cfg_hi < ceil ? cfg_hi : ceil;
    if (hi < r->lo) hi = r->lo;
    r->hi = hi;
    if (r->cur > hi) r->cur = hi;
}

void hb_rate_not_calm(hb_rate *r, long now_ms)
{
    r->t_calm = now_ms;
}

void hb_rate_hold_floor(hb_rate *r, long now_ms, long until)
{
    r->hold_until = until;
    if (r->cur != r->lo) {
        r->cur = r->lo;
        r->t_down = now_ms;
        r->downs++;
    }
    r->t_calm = until;
}

int hb_rate_update(hb_rate *r, long now, int queue, int qmax, long drops)
{
    int dropped = drops > r->last_drops, step = 0;
    int late = queue - HB_RATE_SLACK, room = qmax - HB_RATE_SLACK;
    long late_ms = 0;

    r->last_drops = drops;
    if (now < r->hold_until) {            /* a dip: floor, nothing else */
        r->cur = r->lo;
        r->t_calm = r->hold_until;
        r->t_late = -1;
        return r->cur;
    }
    if (room < 2) room = 2;
    /* Late packets only count once they persist: bursty completion events
     * (e.g. a 96 ms gap) fill the queue for a moment and it drains again
     * by itself; a link that is really too slow keeps it full. */
    if (late > 0) {
        if (r->t_late < 0) r->t_late = now;
        late_ms = now - r->t_late;
    } else {
        r->t_late = -1;
    }
    if (dropped) step = -4;                                          /* lost audio */
    else if (late > 0 && late * 2 >= room && late_ms >= LATE_BIG_MS) step = -3;
    else if (late > 0 && late_ms >= LATE_MS) step = -1;              /* stays behind */

    if (step < 0) {
        r->t_calm = now;
        if (now - r->t_down >= DOWN_GAP_MS && r->cur > r->lo) {
            if (step <= -3) {             /* this bitpool is too much for the link */
                r->cap = r->cur - 1;
                r->t_cap = now;
            }
            r->cur += step;
            if (r->cur < r->lo) r->cur = r->lo;
            r->t_down = now;
            r->t_late = late > 0 ? now : -1;   /* judge the new bitpool afresh */
            r->downs++;
        }
        return r->cur;
    }
    if (late_ms >= LATE_CALM_MS) {        /* not calm */
        r->t_calm = now;
        return r->cur;
    }
    if (now - r->t_calm >= UP_CALM_MS && now - r->t_up >= UP_GAP_MS &&
        now - r->t_down >= UP_CALM_MS && r->cur < r->hi) {
        int top = r->hi;
        if (r->cap && now - r->t_cap < CAP_HOLD_MS && r->cap < top) top = r->cap;
        if (r->cur < top) {
            r->cur += r->cur + 8 <= top ? 2 : 1;
            if (r->cur > top) r->cur = top;
            r->t_up = now;
            r->ups++;
        }
    }
    return r->cur;
}

int hb_rate_settled(const hb_rate *r, long now)
{
    if (!r || r->t_late >= 0) return 0;
    if (now - r->t_calm < UP_CALM_MS) return 0;
    if (now - r->t_down < UP_CALM_MS) return 0;
    return 1;
}

int hb_latency_clamp(int ms)
{
    return ms < HB_LAT_MIN_MS ? HB_LAT_MIN_MS : ms > HB_LAT_MAX_MS ? HB_LAT_MAX_MS : ms;
}

int hb_latency_frames_cap(int target_ms, int rate_hz, int samples_per)
{
    int n;
    if (target_ms >= HB_QUEUE_LOW_MS || rate_hz <= 0 || samples_per <= 0) return 0;
    n = (int)((long)target_ms * rate_hz / ((long)HB_QUEUE_FLOOR_PKTS * samples_per * 1000L));
    return n < 2 ? 2 : n;
}

void hb_latency_estimate(hb_latency *o, int pkt_ms, int queue_x10, int radio_gap_ms,
                         int sink_x10)
{
    o->capture_ms = HB_CAPTURE_MS;
    o->packet_ms = pkt_ms > 0 ? pkt_ms : 0;               /* filling one packet */
    o->queue_ms = queue_x10 > 0 ? (queue_x10 * o->packet_ms + 5) / 10 : 0;
    /* One completion gap on average before the controller has sent it. */
    o->radio_ms = radio_gap_ms > 0 ? (radio_gap_ms > 500 ? 500 : radio_gap_ms) : 0;
    o->sink_reported = sink_x10 > 0;
    o->sink_ms = sink_x10 > 0 ? (sink_x10 + 5) / 10 : HB_SINK_TYPICAL_MS;
    o->total_ms = o->capture_ms + o->packet_ms + o->queue_ms + o->radio_ms + o->sink_ms;
}

void hb_lat_backoff_init(hb_lat_backoff *b)
{
    b->extra_ms = b->bad_s = b->good_s = 0;
}

int hb_lat_effective(const hb_lat_backoff *b, int target)
{
    int t = hb_latency_clamp(target), e;
    if (t >= HB_QUEUE_LOW_MS) return t;
    e = t + b->extra_ms;
    return e > HB_QUEUE_LOW_MS ? HB_QUEUE_LOW_MS : e;
}

int hb_lat_auto_clamp(int ms)
{
    if (ms <= 0) return HB_QUEUE_LOW_MS;
    return ms < HB_LAT_AUTO_MIN_MS ? HB_LAT_AUTO_MIN_MS : ms > HB_LAT_AUTO_MAX_MS ? HB_LAT_AUTO_MAX_MS : ms;
}

void hb_lat_auto_init(hb_lat_auto *a, int start_ms)
{
    a->cur_ms = hb_lat_auto_clamp(start_ms);
    a->fail_ms = 0;
    a->drops = a->win_s = a->good_s = a->safe_ms = 0;
    a->ignore_s = HB_LAT_AUTO_START_S;
}

int hb_lat_auto_tick(hb_lat_auto *a, int d, int *changed)
{
    if (changed) *changed = 0;
    if (d < 0) d = 0;
    if (a->ignore_s > 0) {
        a->ignore_s--;
        return a->cur_ms;
    }
    if (d > 0) {
        a->good_s = 0;
        if (!a->drops) a->win_s = 0;
        a->drops += d;
        if (a->drops >= HB_LAT_AUTO_BAD_DROPS) {
            if (a->cur_ms > a->fail_ms) a->fail_ms = a->cur_ms;
            a->drops = 0;
            if (a->cur_ms < HB_LAT_AUTO_MAX_MS) {
                /* A probe below the floor failed: straight back to the level
                 * that held, not a full step above it. */
                if (a->safe_ms > a->cur_ms && a->safe_ms <= a->cur_ms + HB_LAT_AUTO_UP_MS)
                    a->cur_ms = a->safe_ms;
                else
                    a->cur_ms += HB_LAT_AUTO_UP_MS;
                a->safe_ms = 0;
                if (a->cur_ms > HB_LAT_AUTO_MAX_MS) a->cur_ms = HB_LAT_AUTO_MAX_MS;
                a->ignore_s = HB_LAT_AUTO_SETTLE_S;
                if (changed) *changed = 1;
            }
        }
        return a->cur_ms;
    }
    if (a->drops && ++a->win_s >= HB_LAT_AUTO_BAD_WIN_S) a->drops = 0;   /* an odd drop: forgiven */
    a->good_s++;
    if (a->cur_ms <= HB_LAT_AUTO_MIN_MS) return a->cur_ms;
    if (a->cur_ms - HB_LAT_AUTO_DOWN_MS <= a->fail_ms) {
        /* At the floor: stay, but try below it after a long clean stretch. */
        if (a->good_s < HB_LAT_AUTO_REPROBE_S) return a->cur_ms;
        a->fail_ms = 0;
        a->safe_ms = a->cur_ms;
    } else if (a->good_s < HB_LAT_AUTO_GOOD_S) {
        return a->cur_ms;
    }
    a->cur_ms -= HB_LAT_AUTO_DOWN_MS;
    if (a->cur_ms < HB_LAT_AUTO_MIN_MS) a->cur_ms = HB_LAT_AUTO_MIN_MS;
    a->good_s = 0;
    a->drops = 0;
    a->ignore_s = HB_LAT_AUTO_SETTLE_S;
    if (changed) *changed = -1;
    return a->cur_ms;
}

int hb_lat_backoff_tick(hb_lat_backoff *b, int target, int dpm)
{
    int t = hb_latency_clamp(target);
    if (t >= HB_QUEUE_LOW_MS) {               /* default buffer: nothing to step back */
        hb_lat_backoff_init(b);
        return t;
    }
    if (dpm >= HB_LAT_BAD_DPM) {
        b->good_s = 0;
        if (++b->bad_s >= HB_LAT_BAD_S && t + b->extra_ms < HB_QUEUE_LOW_MS) {
            b->extra_ms += HB_LAT_STEP_MS;
            b->bad_s = 0;
        }
    } else if (dpm == 0) {
        b->bad_s = 0;
        if (++b->good_s >= HB_LAT_GOOD_S && b->extra_ms > 0) {
            b->extra_ms -= HB_LAT_STEP_MS;
            if (b->extra_ms < 0) b->extra_ms = 0;
            b->good_s = 0;
        }
    } else {
        b->bad_s = 0;
    }
    if (t + b->extra_ms > HB_QUEUE_LOW_MS) b->extra_ms = HB_QUEUE_LOW_MS - t;
    return hb_lat_effective(b, t);
}
