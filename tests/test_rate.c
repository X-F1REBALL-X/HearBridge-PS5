/* Developed by X-F1REBALL-X.
 * Host tests for the adaptive bitpool:
 *  1. Simulation: 48 kHz joint stereo SBC (375 frames/s) over a link that
 *     returns a fixed number of credits per second, with the real packer
 *     sizing rule (frames/packet from the MTU and the current frame length)
 *     and the real hb_rate controller. Checks that a slow link converges
 *     with no drops and that a fast one climbs back up.
 *  2. Writes an SBC stream whose bitpool changes mid-stream (35→24→30→45)
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

typedef struct { int bp_end, min_bp, max_bp, per_pkt_end; long drops, late_drops, sent; double pps; } sim_out;

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
    hb_rate_init(&r, 35, 35, 35, 0);
    CHECK(r.lo == 35 && r.hi == 35 && hb_rate_update(&r, 1000, 10, 10, 5) == 35,
          "fixed bitpool (sink took no range): never changes");
    hb_rate_init(&r, 30, 40, 35, 0);
    CHECK(r.lo == 30 && r.hi == 40, "configured range above the floor is kept");
    hb_rate_init(&r, 2, 53, 35, 0);
    CHECK(hb_rate_update(&r, 1000, 6, 10, 0) == 32, "queue half full: -3");
    CHECK(hb_rate_update(&r, 1100, 9, 10, 1) == 32, "at most one step down per 300 ms");
    CHECK(hb_rate_update(&r, 1400, 9, 10, 2) == 28, "drops: -4");
    CHECK(hb_rate_update(&r, 5000, 0, 10, 2) == 28, "no step up before the calm period");
    CHECK(hb_rate_update(&r, 7500, 0, 10, 2) == 29, "step up +1 after 6 s calm");
    CHECK(hb_rate_update(&r, 8000, 0, 10, 2) == 29, "steps up are spaced");

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

    /* Big MTU: up to 15 frames/packet (4-bit NUM field). */
    o = simulate(200.0, 1021, 10, 10);
    CHECK(o.per_pkt_end <= 15 && o.per_pkt_end >= 11, "large MTU: frames/packet capped at 15");

    /* Mid-stream bitpool changes for the external decoder check. */
    if (argc >= 3) {
        static const int seq[] = { 35, 24, 30, 45 };
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
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
