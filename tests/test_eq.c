/* Developed by X-F1REBALL-X. Equalizer: math helpers, flat = passthrough,
 * gain at the band centres, shelves, headroom/limiter, cost. */
#include "eq.h"
#include "gain.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

/* Measured gain (dB) of a sine at hz through the filters (steady state). */
static double measure(hb_eq *e, double hz)
{
    enum { N = 48000 };
    static float b[2 * N];
    double in = 0, out = 0;
    int i;
    for (i = 0; i < N; i++) b[2 * i] = b[2 * i + 1] = (float)(0.1 * sin(2 * M_PI * hz * i / 48000.0));
    for (i = N / 2; i < N; i++) in += (double)b[2 * i] * b[2 * i];
    {
        int z = HB_EQ_NB, k;
        for (k = 0; k < z; k++) memset(e->z[k], 0, sizeof e->z[k]);
    }
    hb_eq_process(e, b, N);
    for (i = N / 2; i < N; i++) out += (double)b[2 * i] * b[2 * i];
    return 10 * log10(out / in);
}

int main(void)
{
    hb_eq e;
    char m[160];
    int i, flat[HB_EQ_NB] = { 0 }, ok;
    double err = 0;

    for (i = -400; i <= 400; i++) {
        double x = i * 0.05, d = fabs(hb_sin(x) - sin(x)) + fabs(hb_cos(x) - cos(x));
        if (d > err) err = d;
    }
    CHECK(err < 1e-6, "hb_sin/hb_cos match libm (|x| <= 20)");
    ok = fabs(hb_pow10(0.3) - pow(10, 0.3)) < 1e-9 && fabs(hb_pow10(-0.6) - pow(10, -0.6)) < 1e-9 &&
         fabs(hb_sqrt(2.0) - sqrt(2.0)) < 1e-12 && fabs(hb_log10(1234.5) - log10(1234.5)) < 1e-9 &&
         fabs(hb_log10(0.002) - log10(0.002)) < 1e-9;
    CHECK(ok, "hb_pow10/hb_sqrt/hb_log10 match libm");

    /* flat and off: bit-exact passthrough */
    {
        enum { N = 4800 };
        static int16_t a[2 * N], b[2 * N];
        srand(1);
        for (i = 0; i < 2 * N; i++) a[i] = (int16_t)(rand() % 60000 - 30000);
        memcpy(b, a, sizeof a);
        hb_eq_init(&e);
        hb_eq_set(&e, 1, flat, 48000);
        gain_apply_soft(a, 2 * N, 1000);
        gain_apply_soft_eq(b, 2 * N, 1000, &e);
        CHECK(!e.on && !memcmp(a, b, sizeof a), "EQ on but flat: bit-exact same output as without EQ");
        {
            int db[HB_EQ_NB] = { 6, 0, 0, 0, 6 };
            memcpy(b, a, sizeof a);
            hb_eq_set(&e, 0, db, 48000);
            gain_apply_soft(a, 2 * N, 1000);
            gain_apply_soft_eq(b, 2 * N, 1000, &e);
            CHECK(!e.on && !memcmp(a, b, sizeof a), "EQ off: bit-exact passthrough");
        }
    }

    /* each band alone at +6 / -6 dB: gain at its centre */
    for (i = 0; i < HB_EQ_NB; i++) {
        int db[HB_EQ_NB] = { 0 }, s;
        for (s = -1; s <= 1; s += 2) {
            double probe = i == 0 ? 25.0 : i == HB_EQ_NB - 1 ? 18000.0 : hb_eq_hz[i], g, want = 6.0 * s, resp;
            db[i] = 6 * s;
            hb_eq_init(&e);
            hb_eq_set(&e, 1, db, 48000);
            g = measure(&e, probe);
            resp = hb_eq_response_db(&e, (float)probe, 48000);
            snprintf(m, sizeof m, "band %d (%d Hz) %+d dB: %.2f dB at %.0f Hz (response calc %.2f)",
                     i, hb_eq_hz[i], 6 * s, g, probe, resp);
            CHECK(fabs(g - want) < (i == 0 || i == HB_EQ_NB - 1 ? 1.0 : 0.3) && fabs(resp - g) < 0.2, m);
        }
    }
    /* a band leaves far-away frequencies alone */
    {
        int db[HB_EQ_NB] = { 0, 0, 9, 0, 0 };
        double g;
        hb_eq_init(&e);
        hb_eq_set(&e, 1, db, 48000);
        g = measure(&e, 60.0);
        snprintf(m, sizeof m, "mid +9 dB: 60 Hz moves %.2f dB", g);
        CHECK(fabs(g) < 0.5, m);
    }
    /* presets */
    {
        static const int presets[][HB_EQ_NB] = { { 6, 3, 0, 0, 0 }, { -6, -3, 0, 0, 0 }, { 0, 0, 0, 3, 6 }, { -3, -1, 2, 5, 2 } };
        static const char *const names[] = { "bass boost", "bass cut", "treble boost", "voice/footsteps" };
        int p;
        for (p = 0; p < 4; p++) {
            double lo, hi;
            hb_eq_init(&e);
            hb_eq_set(&e, 1, presets[p], 48000);
            lo = measure(&e, 40.0);
            hi = measure(&e, 4000.0);
            snprintf(m, sizeof m, "preset %s: 40 Hz %+.1f dB, 4 kHz %+.1f dB, preamp %.2f", names[p], lo, hi, e.preamp);
            CHECK(p == 0 ? lo > 5 : p == 1 ? lo < -5 : p == 2 ? hi > 2 : (hi > 4 && lo < -2), m);
        }
    }
    /* headroom: full-scale input, every band +12, gain x5: no wrap, limiter holds */
    {
        enum { N = 48000 };
        static int16_t a[2 * N];
        int db[HB_EQ_NB] = { 12, 12, 12, 12, 12 }, wraps = 0, pk;
        for (i = 0; i < N; i++) {
            double v = 0.98 * sin(2 * M_PI * 80 * i / 48000.0) + (i % 97 < 48 ? 0.5 : -0.5);
            if (v > 1) v = 1;
            if (v < -1) v = -1;
            a[2 * i] = a[2 * i + 1] = (int16_t)(v * 32767);
        }
        hb_eq_init(&e);
        hb_eq_set(&e, 1, db, 48000);
        CHECK(fabs(e.preamp - pow(10, -12 / 20.0)) < 1e-4, "preamp = minus the largest boost (-12 dB)");
        {
            /* reference: the same chain in float without the int16 output stage */
            static float ref[2 * N];
            hb_eq r;
            hb_eq_init(&r);
            hb_eq_set(&r, 1, db, 48000);
            for (i = 0; i < 2 * N; i++) ref[i] = a[i] * 5.0f * r.preamp / 32768.f;
            hb_eq_process(&r, ref, N);
            pk = gain_apply_soft_eq(a, 2 * N, 5000, &e);
            for (i = 0; i < 2 * N; i++)
                if ((ref[i] > 0.01f && a[i] < 0) || (ref[i] < -0.01f && a[i] > 0)) wraps++;
        }
        snprintf(m, sizeof m, "+12 dB everywhere, gain x5, full-scale input: peak %.3f, %d sign flips (wraparound)",
                 pk / 1000.0, wraps);
        CHECK(pk < 1000 && wraps == 0, m);
    }
    /* cost: 10 s of 48 kHz stereo with all bands active */
    {
        enum { N = 48000 };
        static float b[2 * N];
        int db[HB_EQ_NB] = { 3, -2, 4, -1, 5 }, k;
        clock_t t0;
        double ms;
        for (i = 0; i < 2 * N; i++) b[i] = (float)((i * 7919) % 2001 - 1000) / 1000.f;
        hb_eq_init(&e);
        hb_eq_set(&e, 1, db, 48000);
        t0 = clock();
        for (k = 0; k < 10; k++) hb_eq_process(&e, b, N);
        ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
        snprintf(m, sizeof m, "cost: 10 s of audio, 5 bands, in %.1f ms (%.2f%% of real time)", ms, ms / 100.0);
        CHECK(ms < 1000, m);
    }
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
