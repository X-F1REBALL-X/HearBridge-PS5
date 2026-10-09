/* Developed by X-F1REBALL-X. Link quality meter. */
#include "linkq.h"

#include <stdio.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

int main(void)
{
    hb_linkq q;
    int i, d = 0;
    hb_linkq_init(&q);
    CHECK(hb_linkq_drops_per_min(&q, 0, 1000) == 0, "first sample: 0 drops/min");
    CHECK(hb_linkq_drops_per_min(&q, 0, 2000) == 0, "no drops: 0");
    CHECK(hb_linkq_drops_per_min(&q, 2, 3000) == 120, "a burst shows at once (2 in 1 s = 120/min)");
    for (i = 0; i < 60; i++) d = hb_linkq_drops_per_min(&q, 2, 4000 + i * 1000);
    CHECK(d < 10, "calm again: it settles back down");
    CHECK(hb_linkq_drops_per_min(&q, 0, 70000) == 0, "counter reset (new link): starts over");
    CHECK(hb_linkq_rssi_score(HB_RSSI_UNKNOWN) < 0, "rssi: unknown");
    CHECK(hb_linkq_rssi_score(0) == 100 && hb_linkq_rssi_score(-5) == 80 && hb_linkq_rssi_score(-20) == 20,
          "rssi: golden range relative values");
    CHECK(hb_linkq_rssi_score(-50) == 100 && hb_linkq_rssi_score(-90) == 0, "rssi: absolute dBm values");
    CHECK(hb_linkq_score(0, 255, 0, 0, 10) == 100, "score: perfect link");
    CHECK(hb_linkq_score(0, 255, 30, 0, 10) <= 40, "score: drops pull it down hard");
    CHECK(hb_linkq_score(HB_RSSI_UNKNOWN, -1, 0, 0, 10) == 100, "score: no radio numbers, stream fine");
    CHECK(hb_linkq_score(HB_RSSI_UNKNOWN, -1, -1, 0, 0) == -1, "score: nothing known");
    CHECK(hb_linkq_bars(-1) == 0 && hb_linkq_bars(90) == 4 && hb_linkq_bars(60) == 3 &&
          hb_linkq_bars(40) == 2 && hb_linkq_bars(5) == 1, "bars");
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
