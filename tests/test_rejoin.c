/* Developed by X-F1REBALL-X. After a drop: listen, a few gentle pages, then sit. */
#include "rejoin.h"

#include <stdio.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

int main(void)
{
    int i, prev = 0;
    CHECK(hb_re_paging(0) && hb_re_listen_ms(0) >= 1000 && hb_re_listen_ms(0) <= 2000,
          "after a drop: about a second of listen, then a page");
    CHECK(hb_re_paging(HB_RE_PAGES - 1) && !hb_re_paging(HB_RE_PAGES),
          "only a few pages, then no more");
    CHECK(hb_re_listen_ms(HB_RE_PAGES) == 0 && hb_re_listen_ms(99) == 0,
          "after the pages: sit and accept an incoming connection");
    for (i = 0; i < HB_RE_PAGES; i++) {
        int ms = hb_re_listen_ms(i);
        CHECK(ms > prev && ms <= 3000, i ? "later listens stay short" : "first listen is the short one");
        prev = ms;
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
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
