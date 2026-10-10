/* Developed by X-F1REBALL-X. After a drop: listen, a few gentle pages, then sit. */
#include "rejoin.h"

#include <stdio.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

int main(void)
{
    int i, prev = 0;
    CHECK(hb_re_paging(0) && hb_re_listen_ms(0) >= 1000 && hb_re_listen_ms(0) <= 2000,
          "after a drop: a second or two of listen, then a page");
    CHECK(hb_re_paging(HB_RE_PAGES - 1) && !hb_re_paging(HB_RE_PAGES),
          "only a few pages, then no more");
    CHECK(hb_re_listen_ms(HB_RE_PAGES) == 0 && hb_re_listen_ms(99) == 0,
          "after the pages: sit and accept an incoming connection");
    for (i = 0; i < HB_RE_PAGES; i++) {
        int ms = hb_re_listen_ms(i);
        CHECK(ms > prev && (i ? ms >= 3 * prev : ms <= 2000),
              i ? "later gaps back off (radio left to the pad)" : "first listen is the short one");
        prev = ms;
    }
    {
        /* #29: at most HB_RE_PAGES pages of up to 5 s each; total paging
         * time stays bounded and the radio gets long gaps between them. */
        int tot_gap = 0;
        for (i = 1; i < HB_RE_PAGES; i++) tot_gap += hb_re_listen_ms(i);
        CHECK(HB_RE_PAGES * 5000 <= 15000 && tot_gap >= 60000,
              "rejoin: 15 s of paging at most, a minute of gaps between pages");
        CHECK(hb_auto_gap_ms(0) >= 10000 && hb_auto_gap_ms(1) > hb_auto_gap_ms(0) &&
              hb_auto_gap_ms(2) > hb_auto_gap_ms(1) && hb_auto_gap_ms(HB_AUTO_PAGES) == 0,
              "auto: background pages back off, none after the budget");
    }
    /* rest mode / resume */
    CHECK(!hb_resume_gap(0, 1000, 0, 1000, 5000), "resume: first tick is not a wake");
    CHECK(!hb_resume_gap(1000, 2000, 50000, 51000, 5000), "resume: a normal second is not a wake");
    CHECK(hb_resume_gap(1000, 600000, 50000, 650000, 5000), "resume: both clocks jumped (slept)");
    CHECK(hb_resume_gap(1000, 1500, 50000, 900000, 5000), "resume: wall clock jumped, monotonic stopped");
    CHECK(!hb_resume_gap(1000, 2000, 50000, 40000, 5000), "resume: clock set backwards is not a wake");
    CHECK(hb_reopen_delay_ms(0) < hb_reopen_delay_ms(3) && hb_reopen_delay_ms(50) == 5000,
          "reopen: quick at first, then every 5 s");
    {
        hb_rest r;
        hb_rest_init(&r);
        CHECK(hb_rest_step(&r, 0, 0, 1, 1000) == HB_REST_NONE, "rest: nothing asked, nothing done");
        CHECK(hb_rest_step(&r, 1, 0, 1, 1250) == HB_REST_STOP, "rest: request while playing stops the stream");
        CHECK(hb_rest_step(&r, 1, 0, 0, 1500) == HB_REST_NONE, "rest: stopped once, not again");
        CHECK(hb_rest_step(&r, 0, 1, 0, 900000) == HB_REST_RESUME, "rest: wake brings the headset back");
        CHECK(hb_rest_step(&r, 0, 0, 1, 900250) == HB_REST_NONE, "rest: back to normal after the wake");
        hb_rest_init(&r);
        CHECK(hb_rest_step(&r, 1, 0, 0, 1000) == HB_REST_NONE, "rest: request with nothing playing: no stop");
        CHECK(hb_rest_step(&r, 0, 1, 0, 800000) == HB_REST_NONE, "rest: and nothing to bring back");
        hb_rest_init(&r);
        (void)hb_rest_step(&r, 1, 0, 1, 1000);
        CHECK(hb_rest_step(&r, 0, 0, 0, 2000) == HB_REST_NONE, "rest: request gone, still waiting for sleep");
        CHECK(hb_rest_step(&r, 0, 0, 0, 2000 + HB_REST_GIVEUP_MS) == HB_REST_RESUME,
              "rest: cancelled rest (no sleep) reconnects after 30 s");
    }
    /* DualSense regression: our own blocking page (5 s) and 5 s listen
     * slices looked like a wake, so the headset was paged for ever. */
    CHECK(HB_WAKE_GAP_MS >= 30000, "wake: gap is 30 s or more");
    CHECK(!hb_resume_gap(1000, 1000 + 5200, 50000, 50000 + 5200, HB_WAKE_GAP_MS), "wake: a 5 s page is not a wake");
    CHECK(!hb_resume_gap(1000, 1000 + 8100, 50000, 50000 + 8100, HB_WAKE_GAP_MS), "wake: an 8 s listen is not a wake");
    CHECK(hb_resume_gap(1000, 1000 + 45000, 50000, 50000 + 45000, HB_WAKE_GAP_MS), "wake: 45 s asleep is a wake");
    CHECK(hb_resume_gap(1000, 1500, 50000, 50000 + 600000, HB_WAKE_GAP_MS), "wake: wall clock 10 min ahead is a wake");
    {
        int done = 0, pages = 0;
        while (hb_auto_page_ok(done) && pages < 100) { done++; pages++; }
        CHECK(pages == HB_AUTO_PAGES && HB_AUTO_PAGES == 3, "auto: 3 background pages per window, then only listen");
        CHECK(!hb_auto_page_ok(-1) && hb_auto_page_ok(0), "auto: counter bounds");
    }
    {
        int pages = 0, n = 0;
        while (hb_re_paging(pages) && n < 100) { pages++; n++; }
        CHECK(n == 3 && hb_re_listen_ms(pages) == 0, "rejoin: 3 pages after a drop, then only listen");
    }
    {
        /* hb10.log: the scan ended with ~1.2 s of "deadline reached" spam every
         * ~130 ms. Inquiry and scan loop now share one rule. */
        long dl = 5314325;
        int t, spins = 0;
        CHECK(hb_scan_slots_left(dl, dl - 10000) == 7, "scan: 10 s left fits 7 inquiries of 1.28 s");
        CHECK(hb_scan_slots_left(dl, dl - 1780) == 1 && hb_scan_slots_left(dl, dl - 1779) == 0,
              "scan: the last inquiry must end 500 ms before the deadline");
        CHECK(hb_scan_slots_left(0, 1000) == 0 && hb_scan_slots_left(dl, dl + 50) == 0, "scan: no deadline / past it: nothing fits");
        /* the loop: inquiry skipped -> loop also done, no retries in between */
        for (t = 5313666; t < dl; t += 128)
            if (!hb_scan_slots_left(dl, t)) break; else spins++;
        CHECK(spins == 0, "scan: once no inquiry fits, the scan loop is done at once (no spin)");
        CHECK(HB_PAIR_CALLBACK_MS >= 8000 && HB_PAIR_CALLBACK_MS <= 10000,
              "pairing link hung up: listen 8 to 10 s for the headset to call back before paging");
    }
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
