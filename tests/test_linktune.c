/* Developed by X-F1REBALL-X. Frames/packet tuner (linktune.h) against a
 * simulated link: the hb7.log case (OnePlus Buds Ace 2, slow link) must
 * grow packets instead of shrinking them into ~45 drops/s forever; bursty
 * credits still shrink; a short dip holds the bitpool at its floor and
 * freezes the size; the ceiling grows back once the link is good. */
#include "linktune.h"
#include "rate.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

/* 48 kHz, 128 samples per frame: 2.667 ms of audio per frame. */
static double need_for(int pp) { return 48000.0 / (128.0 * pp); }

typedef struct { int per_pkt; int fit; hb_tune t; long drops_total; } sim;

static int pp_of(const sim *s) { return s->t.max_pp > 0 && s->t.max_pp < s->fit ? s->t.max_pp : s->fit; }

/* One second of link: credits per second the radio returns, queue state. */
static int sim_step(sim *s, long now, double cred_pps, int backlog, int cap)
{
    hb_tune_in in;
    double need = need_for(s->per_pkt);
    int drops = cred_pps < need ? (int)(need - cred_pps + 0.5) : 0, a;
    memset(&in, 0, sizeof in);
    in.now = now; in.per_pkt = s->per_pkt; in.fit_pp = s->fit;
    in.cred_pps = cred_pps; in.need_pps = need; in.new_drops = drops;
    in.backlog = backlog; in.cap = cap;
    a = hb_tune_step(&s->t, &in);
    s->per_pkt = pp_of(s);
    s->drops_total += drops;
    return a;
}

int main(void)
{
    sim s;
    int i, a, shrinks = 0, last_drops = 0, dips = 0;
    long now;

    /* 1) hb7.log: the link gives 13..29 credits/s, 13 frames/packet need 29/s.
     * Old code: 13 -> 11 -> 9 -> 7 -> 5 frames/packet, 75 packets/s needed. */
    memset(&s, 0, sizeof s);
    hb_tune_init(&s.t);
    s.fit = 13; s.per_pkt = 13;
    s.t.max_pp = 9;                      /* already shrunk by a bad moment */
    s.per_pkt = 9;
    for (i = 0, now = 0; i < 30; i++, now += 1000) {
        a = sim_step(&s, now, 20.0, 9, 10);
        if (a == HB_TUNE_SHRINK) shrinks++;
        if (a == HB_TUNE_DIP) dips++;
        if (i == 29) last_drops = (int)(need_for(s.per_pkt) - 20.0 + 0.5);
    }
    CHECK(shrinks == 0, "slow link (20 credits/s for 34 packets/s): never smaller packets");
    CHECK(s.per_pkt == 13 && s.t.max_pp == 0, "slow link: packets grow back to the MTU fit (fewer per second)");
    CHECK(dips <= 1, "slow link: the first second counts as a dip, then the tuner acts");
    printf("     slow link: %d frames/packet, %.0f packets/s needed, %d drops/s left (bitpool controller takes it from here)\n",
           s.per_pkt, need_for(s.per_pkt), last_drops > 0 ? last_drops : 0);

    /* 2) bursty credits: enough on average (40/s for 29 needed) but drops */
    memset(&s, 0, sizeof s);
    hb_tune_init(&s.t);
    s.fit = 13; s.per_pkt = 13;
    {
        hb_tune_in in;
        memset(&in, 0, sizeof in);
        for (i = 0, now = 100000, shrinks = 0; i < 6; i++, now += 1000) {
            in.now = now; in.per_pkt = s.per_pkt; in.fit_pp = s.fit; in.cred_pps = 40; in.need_pps = need_for(s.per_pkt);
            in.new_drops = 2; in.backlog = 2; in.cap = 10;   /* queue nearly empty: no dip */
            if (hb_tune_step(&s.t, &in) == HB_TUNE_SHRINK) shrinks++;
            s.per_pkt = pp_of(&s);
        }
    }
    CHECK(shrinks >= 1 && s.per_pkt < 13 && s.per_pkt >= HB_TUNE_MIN_PP, "bursty credits with enough on average: smaller packets");

    /* 3) a short dip on a good link: freeze 5 s, bitpool at the floor */
    memset(&s, 0, sizeof s);
    hb_tune_init(&s.t);
    s.fit = 13; s.per_pkt = 13;
    for (i = 0, now = 200000; i < 5; i++, now += 1000) (void)sim_step(&s, now, 45.0, 1, 10);   /* calm */
    a = sim_step(&s, now, 10.0, 9, 10);  /* the dip: credits stall, queue full */
    CHECK(a == HB_TUNE_DIP && hb_tune_frozen(&s.t, now + 4000) && !hb_tune_frozen(&s.t, now + HB_TUNE_FREEZE_MS),
          "dip: detected, packet size frozen for 5 s");
    {
        hb_rate r;
        hb_rate_init(&r, 2, 53, 51, now);
        hb_rate_hold_floor(&r, now, s.t.freeze_until);
        CHECK(r.cur == r.lo && hb_rate_update(&r, now + 2000, 0, 10, 0) == r.lo &&
              hb_rate_update(&r, now + 4900, 0, 10, 0) == r.lo, "dip: bitpool held at its floor during the freeze");
        CHECK(!hb_rate_settled(&r, now + 6000), "dip: no step up right after the freeze (calm starts when it ends)");
    }
    now += 1000;
    for (i = 0; i < 3; i++, now += 1000) {
        a = sim_step(&s, now, 12.0, 9, 10);
        CHECK(a == HB_TUNE_KEEP && s.per_pkt == 13, "dip: no packet-size change while frozen");
    }
    for (i = 0; i < 12; i++, now += 1000) (void)sim_step(&s, now, 45.0, 1, 10);   /* link is back */
    CHECK(s.per_pkt == 13 && s.t.max_pp == 0, "short dip: nothing locked in afterwards");

    /* 4) recovery: a ceiling from a bad stretch grows back after 10 s calm */
    memset(&s, 0, sizeof s);
    hb_tune_init(&s.t);
    s.fit = 13; s.t.max_pp = 5; s.per_pkt = 5;
    {
        int grew_at = -1;
        for (i = 0, now = 300000; i < 40; i++, now += 1000) {
            a = sim_step(&s, now, need_for(s.per_pkt) * 1.5, 0, 10);
            if (a == HB_TUNE_RECOVER && grew_at < 0) grew_at = i;
        }
        CHECK(grew_at >= 0 && s.per_pkt > 5, "recovery: the ceiling grows back with credits to spare");
        CHECK(s.t.max_pp == 0 || s.per_pkt >= 9, "recovery: two frames per step, every 10 s");
    }
    /* no recovery without headroom */
    memset(&s, 0, sizeof s);
    hb_tune_init(&s.t);
    s.fit = 13; s.t.max_pp = 7; s.per_pkt = 7;
    for (i = 0, now = 400000; i < 30; i++, now += 1000) (void)sim_step(&s, now, need_for(s.per_pkt) * 1.1, 0, 10);
    CHECK(s.per_pkt == 7, "no recovery while credits have under 20 % headroom");
    (void)dips;
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
