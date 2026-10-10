/* Developed by X-F1REBALL-X.
 * Host tests for the adaptive bitpool:
 *  1. Simulation: 48 kHz joint stereo SBC (375 frames/s) over a link that
 *     returns a fixed number of credits per second, with the real packer
 *     sizing rule (frames/packet from the MTU and the current frame length)
 *     and the real hb_rate controller. Checks that a slow link converges
 *     with no drops and that a fast one climbs back up.
 *  2. Writes an SBC stream whose bitpool changes mid-stream (35→24→30→53)
 *     for an external decoder check (ffmpeg + SNR in the Makefile). */
#include "rate.h"
#include "sbc.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void log_line(const char *fmt, ...) { (void)fmt; }

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

/* SBC frame length, 16 blocks, 8 subbands, joint stereo (A2DP 12.9). */
static int frame_len(int bp) { return 4 + 8 + (8 + 16 * bp + 7) / 8; }

typedef struct { int bp_end, min_bp, max_bp, per_pkt_end, max_queue; long drops, late_drops, sent; double pps;
                 double max_queue_ms; } sim_out;

/* credits_per_s: packets the link delivers per second; secs simulated. */
static sim_out simulate(double credits_per_s, int mtu, int secs, int qmax)
{
    hb_rate r;
    sim_out o;
    double credit = 0, frames_due = 0;
    int queue = 0, pending = 0, per_pkt;
    long t, drops = 0;

    memset(&o, 0, sizeof o);
    hb_rate_init(&r, 2, 53, 35, 0);
    o.min_bp = o.max_bp = r.cur;
    per_pkt = (mtu - 13) / frame_len(r.cur);
    if (per_pkt > 15) per_pkt = 15;
    for (t = 0; t < secs * 1000L; t++) {            /* 1 ms steps */
        credit += credits_per_s / 1000.0;
        while (credit >= 1 && queue > 0) { queue--; credit -= 1; o.sent++; }
        if (queue == 0 && credit > 2) credit = 2;    /* unused credits don't pile up */
        frames_due += 375.0 / 1000.0;
        while (frames_due >= 1) {
            frames_due -= 1;
            if (++pending >= per_pkt) {              /* packet complete */
                pending = 0;
                if (queue >= qmax) {                 /* oldest media packet dropped */
                    queue--;
                    drops++;
                    if (t > secs * 1000L / 2) o.late_drops++;
                }
                queue++;
                if (queue > o.max_queue) o.max_queue = queue;
                if (queue * per_pkt * 128000.0 / 48000.0 > o.max_queue_ms)
                    o.max_queue_ms = queue * per_pkt * 128000.0 / 48000.0;
                r.cur = hb_rate_update(&r, t, queue, qmax, drops);
                per_pkt = (mtu - 13) / frame_len(r.cur);
                if (per_pkt > 15) per_pkt = 15;
                if (r.cur < o.min_bp) o.min_bp = r.cur;
                if (r.cur > o.max_bp) o.max_bp = r.cur;
            }
        }
    }
    o.bp_end = r.cur;
    o.per_pkt_end = per_pkt;
    o.drops = drops;
    o.pps = 375.0 / per_pkt;
    return o;
}

int main(int argc, char **argv)
{
    sim_out o;
    char m[160];
    hb_rate r;

    /* controller basics */
    hb_rate_init(&r, 2, 53, 35, 0);
    CHECK(r.lo == HB_RATE_FLOOR && r.hi == HB_RATE_CEIL && r.cur == 35, "range clamps to floor/ceiling");
    CHECK(HB_RATE_CEIL == 53 && r.hi == 53, "sink max 53: the controller may climb to 53");
    hb_rate_init(&r, 2, 51, 35, 0);
    CHECK(r.hi == 51, "sink max 51: the sink's own maximum is the limit");
    hb_rate_init(&r, 2, 64, 35, 0);
    CHECK(r.hi == 53, "sink max above 53: capped at 53");
    hb_rate_init(&r, 35, 35, 35, 0);
    CHECK(r.lo == 35 && r.hi == 35 && hb_rate_update(&r, 1000, 10, 10, 5) == 35,
          "fixed bitpool (sink took no range): never changes");
    hb_rate_init(&r, 30, 40, 35, 0);
    CHECK(r.lo == 30 && r.hi == 40, "configured range above the floor is kept");
    hb_rate_init(&r, 2, 53, 35, 0);
    /* qmax 20: room above the slack (4 paced packets) is 16. */
    CHECK(hb_rate_update(&r, 500, HB_RATE_SLACK, 20, 0) == 35, "paced packets only (queue = slack): no change");
    CHECK(hb_rate_update(&r, 1000, HB_RATE_SLACK + 8, 20, 0) == 35, "a short burst of late packets: no change");
    CHECK(hb_rate_update(&r, 1350, HB_RATE_SLACK + 8, 20, 0) == 32, "half the room late for 300 ms: -3");
    CHECK(hb_rate_update(&r, 1400, 19, 20, 1) == 32, "at most one step down per 300 ms");
    CHECK(hb_rate_update(&r, 1700, 19, 20, 2) == 28, "drops: -4");
    CHECK(r.cap == 31, "congested at 32: temporary ceiling 31");
    CHECK(hb_rate_update(&r, 1800, HB_RATE_SLACK + 2, 20, 2) == 28, "a few late packets for 100 ms: no change");
    CHECK(hb_rate_update(&r, 2400, HB_RATE_SLACK + 2, 20, 2) == 27, "late packets staying 600 ms: -1");
    CHECK(hb_rate_update(&r, 5000, 0, 20, 2) == 27, "no step up before the calm period");
    CHECK(!hb_rate_settled(&r, 5000), "still dropping or not calm yet: do not treat the bitpool as held");
    CHECK(hb_rate_update(&r, 6500, HB_RATE_SLACK, 20, 2) == 28,
          "step up after 4 s calm (paced packets count as calm); +1 near the ceiling");
    CHECK(hb_rate_settled(&r, 6500), "clean for the calm window: this bitpool can be remembered");
    CHECK(hb_rate_update(&r, 7000, 0, 20, 2) == 28, "steps up are spaced");
    hb_rate_update(&r, 8500, 0, 20, 2); hb_rate_update(&r, 10500, 0, 20, 2);
    hb_rate_update(&r, 12500, 0, 20, 2);
    CHECK(hb_rate_update(&r, 14500, 0, 20, 2) == 31, "does not retry the congested bitpool within 30 s");
    CHECK(hb_rate_update(&r, 31800, 0, 20, 2) == 33, "retries above it after 30 s (+2)");
    hb_rate_init(&r, 2, 53, 22, 0);
    CHECK(hb_rate_update(&r, 4000, 0, 8, 0) == 24, "far below the top: +2 per step");
    hb_rate_init(&r, 2, 60, 35, 0);
    hb_rate_set_ceiling(&r, 60, 60);
    CHECK(r.hi == 60, "high-quality mode: ceiling lifted to the sink's 60");
    hb_rate_set_ceiling(&r, 60, HB_RATE_CEIL);
    CHECK(r.hi == HB_RATE_CEIL, "standard mode: ceiling 53");

    /* The console case: 8 frames/packet at bitpool 35 (47 pkt/s) vs ~44 credits/s. */
    o = simulate(44.0, 672, 120, 10);
    snprintf(m, sizeof m, "44 pkt/s link: settles at bitpool %d (%d frames/pkt, %.1f pkt/s), "
             "%ld drops total, %ld in 2nd minute", o.bp_end, o.per_pkt_end, o.pps, o.drops, o.late_drops);
    CHECK(o.late_drops == 0 && o.bp_end >= HB_RATE_FLOOR && o.bp_end <= 32 && o.pps < 44.0, m);
    CHECK(o.drops <= 10, "44 pkt/s link: only a few drops while adapting");

    /* Very slow link: hits the floor, drops only what the floor cannot carry. */
    o = simulate(30.0, 672, 60, 10);
    snprintf(m, sizeof m, "30 pkt/s link: floor %d reached (min seen %d)", HB_RATE_FLOOR, o.min_bp);
    CHECK(o.min_bp == HB_RATE_FLOOR, m);

    /* Fast link: climbs to the ceiling, no drops. */
    o = simulate(200.0, 672, 120, 10);
    snprintf(m, sizeof m, "fast link: climbs to %d with %ld drops", o.bp_end, o.drops);
    CHECK(o.bp_end == HB_RATE_CEIL && o.drops == 0, m);
    /* Link fast enough for ~bitpool 45 only (5 frames/packet at 53 = 75 pkt/s):
     * must not get stuck dropping at 53. */
    o = simulate(70.0, 672, 180, hb_media_queue_cap(21, HB_QUEUE_LOW_MS));
    snprintf(m, sizeof m, "70 pkt/s link: settles at bitpool %d, %ld drops in the 2nd half", o.bp_end, o.late_drops);
    CHECK(o.late_drops <= 2 && o.bp_end < 53, m);

    /* Big MTU: up to 15 frames/packet (4-bit NUM field). */
    o = simulate(200.0, 1021, 3, 10);
    CHECK(o.per_pkt_end <= 15 && o.per_pkt_end >= 11, "large MTU: frames/packet capped at 15");

    /* Media queue depth = worst-case added latency. */
    CHECK(hb_media_queue_cap(21, HB_QUEUE_LOW_MS) == 10, "low latency: 10 x 21 ms packets (~200 ms)");
    CHECK(hb_media_queue_cap(13, HB_QUEUE_LOW_MS) == 15, "low latency: 15 x 13 ms packets (~200 ms)");
    CHECK(hb_media_queue_cap(40, HB_QUEUE_LOW_MS) == 5, "low latency: 5 x 40 ms packets (15 frames/packet)");
    CHECK(hb_media_queue_cap(80, HB_QUEUE_LOW_MS) == HB_QUEUE_MIN_PKTS, "queue never below the minimum");
    CHECK(hb_media_queue_cap(0, HB_QUEUE_LOW_MS) == HB_QUEUE_LOW_MS, "0 ms packets treated as 1 ms");
    CHECK(hb_media_queue_cap(21, HB_QUEUE_STABLE_MS) == 48, "stable: 48 x 21 ms packets (~1 s, as in 1.0.x)");
    {
        int pm, worst = 0;
        for (pm = 5; pm <= 40; pm++) {
            int ms = hb_media_queue_cap(pm, HB_QUEUE_LOW_MS) * pm;
            if (ms > worst) worst = ms;
        }
        snprintf(m, sizeof m, "low latency: worst queue %d ms for 5..40 ms packets", worst);
        CHECK(worst <= 230, m);
    }

    /* The console case again with the low-latency queue (8 frames/packet,
     * 21 ms): converges without late drops and the queue never holds more
     * than ~200 ms of audio. */
    {
        int qlow = hb_media_queue_cap(21, HB_QUEUE_LOW_MS);
        o = simulate(44.0, 672, 120, qlow);
        snprintf(m, sizeof m, "low latency, 44 pkt/s link: bitpool %d, max queue %d pkts = %.0f ms, %ld drops "
                 "(%ld in 2nd minute)", o.bp_end, o.max_queue, o.max_queue_ms, o.drops, o.late_drops);
        CHECK(o.late_drops == 0 && o.max_queue <= qlow && o.max_queue_ms <= 230.0, m);
        o = simulate(200.0, 672, 60, qlow);
        CHECK(o.drops == 0 && o.max_queue_ms <= 230.0, "low latency, fast link: no drops, queue under 230 ms");
        o = simulate(44.0, 672, 120, hb_media_queue_cap(21, HB_QUEUE_STABLE_MS));
        CHECK(o.late_drops == 0, "stable mode, 44 pkt/s link: no late drops");
    }

    /* Mid-stream bitpool changes for the external decoder check. */
    if (argc >= 3) {
        static const int seq[] = { 35, 24, 30, 53 };
        sbc_config cfg;
        sbc_encoder *e;
        FILE *fo = fopen(argv[1], "wb"), *fr = fopen(argv[2], "wb");
        int fs = 48000, n = fs * 4, i, k, bad = 0;
        int16_t *pcm = malloc(sizeof *pcm * 2 * (size_t)n);
        unsigned char out[4096];

        memset(&cfg, 0, sizeof cfg);
        cfg.sample_rate = fs; cfg.channels = 2; cfg.joint_stereo = 1; cfg.bitpool = 35;
        e = sbc_encoder_open(&cfg);
        for (i = 0; i < n; i++) {
            double tt = (double)i / fs;
            pcm[2 * i] = (int16_t)lrint(12000.0 * sin(2 * M_PI * 1000.0 * tt));
            pcm[2 * i + 1] = (int16_t)lrint(10000.0 * sin(2 * M_PI * 523.25 * tt));
        }
        fwrite(pcm, 4, (size_t)n, fr);
        for (k = 0; k < 4; k++) {
            int bp = sbc_encoder_set_bitpool(e, seq[k]);
            int from = k * n / 4, to = (k + 1) * n / 4;
            for (i = from; i < to; i += 128) {
                int got = sbc_encoder_encode(e, pcm + 2 * i, 128, out, sizeof out);
                if (got > 0) {
                    if (out[2] != bp || got != frame_len(bp)) bad++;
                    fwrite(out, 1, (size_t)got, fo);
                }
            }
        }
        fclose(fo); fclose(fr); free(pcm); sbc_encoder_close(e);
        CHECK(!bad, "mid-stream bitpool change: every frame header and length match");
    }
    {   /* latency slider + estimate */
        hb_latency L;
        int t, fit = 1;
        CHECK(hb_latency_clamp(10) == 40 && hb_latency_clamp(5000) == 400 && hb_latency_clamp(300) == 300 && hb_latency_clamp(180) == 180,
              "latency target clamps to 40..400 ms");
        {
            hb_lat_backoff b;
            int k, e = 0;
            hb_lat_backoff_init(&b);
            CHECK(hb_lat_backoff_tick(&b, 200, 50) == 200 && b.extra_ms == 0, "backoff: nothing at the 200 ms default");
            for (k = 0; k < 60; k++) e = hb_lat_backoff_tick(&b, 40, 0);
            CHECK(e == 40, "backoff: calm link stays at the low setting");
            for (k = 0; k < 30; k++) e = hb_lat_backoff_tick(&b, 40, 20);
            CHECK(e > 40 && e <= 200, "backoff: drops step the buffer up, never past 200 ms");
            for (k = 0; k < 600; k++) e = hb_lat_backoff_tick(&b, 40, 0);
            CHECK(e == 40 && b.extra_ms == 0, "backoff: calm again, back to the user's setting");
        }
        CHECK(hb_latency_frames_cap(200, 48000, 128) == 0 && hb_latency_frames_cap(1000, 48000, 128) == 0,
              "200 ms and up: packets stay MTU-sized");
        for (t = 60; t < 200; t += 10) {
            int n = hb_latency_frames_cap(t, 48000, 128);
            if (n < 2 || (n > 2 && HB_QUEUE_FLOOR_PKTS * n * 128 * 1000 / 48000 > t)) fit = 0;
        }
        CHECK(fit && hb_latency_frames_cap(60, 48000, 128) == 2 && hb_latency_frames_cap(100, 48000, 128) == 4,
              "below 200 ms: the 8-packet queue floor fits in the target (2 frames minimum)");
        hb_latency_estimate(&L, 11, 20, 24, 0);
        CHECK(L.queue_ms == 22 && L.sink_ms == HB_SINK_TYPICAL_MS && !L.sink_reported &&
              L.total_ms == HB_CAPTURE_MS + 11 + 22 + 24 + HB_SINK_TYPICAL_MS,
              "estimate: capture + packet + queue + radio + typical headset buffer");
        hb_latency_estimate(&L, 29, 0, 0, 1305);
        CHECK(L.sink_ms == 131 && L.sink_reported && L.queue_ms == 0 && L.total_ms == HB_CAPTURE_MS + 29 + 131,
              "estimate: headset delay report (1/10 ms) replaces the typical value");
        hb_latency_estimate(&L, 29, 10, 4000, 0);
        CHECK(L.radio_ms == 500, "estimate: a stalled radio counts at most 500 ms");
    }
    {
        /* Adaptive latency (Auto). Simulated link: stalls longer than
         * `need` ms come every few seconds, so a target below it drops
         * about one packet every 4 s (the Coral CM835 on 1.3.0: ~15
         * drops/min at 200 ms), a target at or above it never drops. */
        static const int needs[] = { 250, 120, 0 };
        unsigned k;
        for (k = 0; k < sizeof needs / sizeof *needs; k++) {
            hb_lat_auto a;
            int t, need = needs[k], late_drops = 0, swings = 0, ch, minv = 1000, maxv = 0;
            char what[160];
            hb_lat_auto_init(&a, 200);
            for (t = 0; t < 2 * 3600; t++) {
                int d = a.cur_ms < need && t % 4 == 0;
                (void)hb_lat_auto_tick(&a, d, &ch);
                if (t >= 600) {
                    late_drops += d;
                    if (ch) swings++;
                    if (a.cur_ms < minv) minv = a.cur_ms;
                    if (a.cur_ms > maxv) maxv = a.cur_ms;
                }
            }
            if (need <= HB_LAT_AUTO_MIN_MS) {
                snprintf(what, sizeof what, "auto: a clean link goes down to %d ms and stays (%d ms)",
                         HB_LAT_AUTO_MIN_MS, a.cur_ms);
                CHECK(a.cur_ms == HB_LAT_AUTO_MIN_MS && late_drops == 0, what);
            } else {
                snprintf(what, sizeof what,
                         "auto: link needing %d ms settles at %d ms (range %d-%d), %d drops in the last 110 min",
                         need, a.cur_ms, minv, maxv, late_drops);
                CHECK(a.cur_ms >= need && a.cur_ms <= need + HB_LAT_AUTO_UP_MS &&
                      late_drops <= 3 * 4 && swings <= 12, what);
            }
        }
        {
            /* The fix/1.3.1 console log: ~8 drops a second (credits starved,
             * RSSI fine) drove Auto 200 -> 400 ms, and the drops stayed.
             * That is a link problem: no raise at all. */
            hb_lat_auto a;
            int t, ch, ups = 0;
            hb_lat_auto_init(&a, 200);
            for (t = 0; t < 600; t++) {
                (void)hb_lat_auto_tick(&a, 8, &ch);
                if (ch > 0) ups++;
            }
            CHECK(a.cur_ms <= 200 + HB_LAT_AUTO_UP_MS && ups <= 1 && a.link_bad,
                  "auto: a huge drop rate is a link problem, at most one step (before the window fills), then held");
            /* Stalls first (raised), then the link falls apart: held where
             * it is (a smaller buffer would only drop more), no more raises.
             * The bad944d console log: 240 -> 200 under a flood made it worse. */
            hb_lat_auto_init(&a, 200);
            for (t = 0; t < 60; t++) (void)hb_lat_auto_tick(&a, t % 3 == 0, &ch);
            CHECK(a.cur_ms > 200, "auto: occasional stalls raise the buffer");
            {
                int held = a.cur_ms, downs = 0, ups2 = 0;
                for (t = 0; t < 120; t++) {
                    (void)hb_lat_auto_tick(&a, 5, &ch);
                    if (ch < 0) downs++;
                    if (ch > 0) ups2++;
                }
                CHECK(a.cur_ms == held && a.link_bad && !downs && !ups2,
                      "auto: then a flood of drops: the level is held, never lowered or raised");
            }
            /* The 9317a4b console log (Coral CM835): clean at 200 ms, Auto
             * stepped to 190 and drops flooded at once (350/min); it raised
             * to 230 and then held it as a link problem. Now: straight back
             * to 200, the queue trimmed, 200 is the floor for the stream. */
            {
                int down_at = -1, back_at = -1, low = 1000, k;
                hb_lat_auto_init(&a, 200);
                for (t = 0; t < 400; t++) {
                    int d = (down_at >= 0 && t - down_at >= 1 && t - down_at < 6) ? 6 : 0;   /* stops once back at 200 */
                    (void)hb_lat_auto_tick(&a, d, &ch);
                    if (ch < 0 && down_at < 0) down_at = t;
                    if (ch > 0 && a.recover && back_at < 0) { back_at = t; a.recover = 0; }
                }
                CHECK(down_at > 0 && back_at > down_at && back_at - down_at <= 3 && a.floor_ms == 200,
                      "auto: drops right after a step down: back to the level that held at once");
                CHECK(a.cur_ms == 200, "auto: no raise above the level that held, no link-problem hold");
                for (k = 0; k < 7200; k++) {
                    (void)hb_lat_auto_tick(&a, 0, &ch);
                    if (a.cur_ms < low) low = a.cur_ms;
                }
                CHECK(low == 200, "auto: never below that level again this stream");
            }
            {
                int first = -1;
                hb_lat_auto_init(&a, 200);
                for (t = 0; t < 400 && first < 0; t++) {
                    (void)hb_lat_auto_tick(&a, t == 15 ? 1 : 0, &ch);
                    if (ch < 0) first = t;
                }
                CHECK(first >= 15 + HB_LAT_AUTO_GOOD_DROP_S, "auto: a stream that dropped steps down only after 90 s clean");
                hb_lat_auto_init(&a, 200);
                for (first = -1, t = 0; t < 400 && first < 0; t++) {
                    (void)hb_lat_auto_tick(&a, 0, &ch);
                    if (ch < 0) first = t;
                }
                CHECK(first > 0 && first < HB_LAT_AUTO_START_S + HB_LAT_AUTO_GOOD_DROP_S, "auto: a clean stream steps down after 45 s");
            }
            /* A link that needs more than any buffer: never above two steps. */
            hb_lat_auto_init(&a, 200);
            for (t = 0; t < 3600; t++) (void)hb_lat_auto_tick(&a, t % 4 == 0, &ch);
            CHECK(a.cur_ms == HB_LAT_AUTO_MAX_MS, "auto: a link needing more stops at 260 ms, never 400");
            hb_lat_auto_init(&a, 100);
            for (t = 0; t < 3600; t++) (void)hb_lat_auto_tick(&a, t % 4 == 0, &ch);
            CHECK(a.cur_ms == 100 + HB_LAT_AUTO_MAX_RAISES * HB_LAT_AUTO_UP_MS,
                  "auto: at most two raises above where the stream started");
            CHECK(HB_LAT_AUTO_MAX_MS <= 260, "auto: ceiling 260 ms");
        }
        {
            hb_lat_auto a;
            int ch, i, r = 0;
            hb_lat_auto_init(&a, 0);
            CHECK(a.cur_ms == 200, "auto: nothing learned yet starts at 200 ms");
            hb_lat_auto_init(&a, 130);
            for (i = 0; i < HB_LAT_AUTO_START_S; i++) r = hb_lat_auto_tick(&a, 5, &ch);
            CHECK(r == 130 && !ch, "auto: drops while the stream starts do not count");
            r = hb_lat_auto_tick(&a, 1, &ch);
            for (i = 0; i < HB_LAT_AUTO_WIN_S; i++) r = hb_lat_auto_tick(&a, 0, &ch);
            r = hb_lat_auto_tick(&a, 1, &ch);
            CHECK(r == 130, "auto: an odd drop now and then is forgiven");
            r = hb_lat_auto_tick(&a, 3, &ch);
            CHECK(r == 130 + HB_LAT_AUTO_UP_MS && ch == 1, "auto: a burst of drops raises the target at once");
            hb_lat_auto_init(&a, 400);
            CHECK(a.cur_ms == 200, "auto: a saved 400 ms (bad build) is not trusted, starts at 200 ms");
            hb_lat_auto_init(&a, 240);
            CHECK(a.cur_ms == 240 && hb_lat_auto_clamp(10) == HB_LAT_AUTO_MIN_MS && hb_lat_auto_clamp(9999) == HB_LAT_AUTO_MAX_MS,
                  "auto: a sane learned value is kept, all within 60 to 260 ms");
        }
    }
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
