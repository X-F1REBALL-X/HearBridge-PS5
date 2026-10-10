/* Developed by X-F1REBALL-X. */
#include "rejoin.h"

int hb_re_listen_ms(int pages)
{
    /* Back off (#29): every page holds the radio up to 5 s, and the pad
     * shares it. First one soon (the headset may just have glitched). */
    static const int ms[HB_RE_PAGES] = { 2000, 15000, 45000 };
    if (pages < 0) pages = 0;
    if (pages >= HB_RE_PAGES) return 0;
    return ms[pages];
}

int hb_re_paging(int pages)
{
    return pages >= 0 && pages < HB_RE_PAGES;
}

int hb_auto_page_ok(int done)
{
    return done >= 0 && done < HB_AUTO_PAGES;
}

int hb_auto_gap_ms(int done)
{
    static const int ms[HB_AUTO_PAGES] = { 12000, 30000, 90000 };
    if (done < 0) done = 0;
    if (done >= HB_AUTO_PAGES) return 0;
    return ms[done];
}

int hb_resume_gap(long mp, long mn, long rp, long rn, long gap)
{
    long dm = mn - mp, dr = rn - rp;
    if (!mp && !rp) return 0;                   /* first tick */
    if (dm > gap) return 1;
    /* wall clock jumped forward while the monotonic one barely moved (and
     * not just a clock set: also needs a real gap on the wall clock) */
    return dr > gap && dr - dm > gap;
}

int hb_reopen_delay_ms(int attempt)
{
    static const int ms[4] = { 1500, 2500, 4000, 5000 };
    if (attempt < 0) attempt = 0;
    return attempt < 4 ? ms[attempt] : 5000;
}

void hb_rest_init(hb_rest *r)
{
    r->down = r->had_stream = 0;
    r->quiet_since = 0;
}

int hb_rest_step(hb_rest *r, int going_down, int woke, int streaming, long now)
{
    if (!r->down) {
        if (!going_down) return HB_REST_NONE;
        r->down = 1;
        r->had_stream = streaming != 0;
        r->quiet_since = 0;
        return r->had_stream ? HB_REST_STOP : HB_REST_NONE;
    }
    if (going_down) {
        r->quiet_since = 0;
        if (!woke) return HB_REST_NONE;
    } else if (!woke) {
        if (!r->quiet_since) r->quiet_since = now ? now : 1;
        if (now - r->quiet_since < HB_REST_GIVEUP_MS) return HB_REST_NONE;
    }
    r->down = 0;
    r->quiet_since = 0;
    if (!r->had_stream) return HB_REST_NONE;
    r->had_stream = 0;
    return HB_REST_RESUME;
}

int hb_scan_slots_left(long deadline, long now)
{
    long left = deadline - now - 500;
    if (deadline <= 0 || left < 1280) return 0;
    return (int)(left / 1280);
}
