/* Developed by X-F1REBALL-X. Night mode compressor (host test). */
#include "night.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

#define RATE 48000
static short buf[RATE * 2];

/* 1 kHz sine at `db` dBFS for `ms`, through the compressor; returns the
 * output peak (dBFS) over the last quarter (settled). */
static double run(hb_night *n, double db, int ms)
{
    int frames = RATE * ms / 1000, i, pk = 0;
    double a = pow(10, db / 20) * 32767;
    for (i = 0; i < frames; i++) {
        short v = (short)lrint(a * sin(2 * M_PI * 1000 * i / RATE));
        buf[2 * i] = buf[2 * i + 1] = v;
    }
    hb_night_process(n, buf, frames);
    for (i = frames * 3 / 4; i < frames; i++) {
        int v = buf[2 * i] < 0 ? -buf[2 * i] : buf[2 * i];
        if (v > pk) pk = v;
    }
    return pk ? 20 * log10(pk / 32767.0) : -200;
}

int main(void)
{
    hb_night n;
    double loud, quiet, off;
    short z[64];
    int i;

    CHECK(fabs(hb_night_curve_db(-50) - HB_NIGHT_MAKEUP_DB) < .01, "curve: quiet sound gets the full lift");
    CHECK(hb_night_curve_db(0) < -10, "curve: full scale comes down by more than 10 dB");
    CHECK(hb_night_curve_db(-90) == 0, "curve: no lift for near silence (hiss stays down)");
    CHECK(hb_night_curve_db(-66) > 0 && hb_night_curve_db(-66) < HB_NIGHT_MAKEUP_DB, "curve: lift fades in under the floor");

    hb_night_init(&n, RATE);
    off = run(&n, -6, 200);
    CHECK(fabs(off - (-6)) < .1, "off: audio untouched");

    hb_night_set(&n, 1);
    loud = run(&n, -3, 500);
    CHECK(loud < -10, "explosion (-3 dBFS) comes down below -10 dBFS");
    hb_night_init(&n, RATE);
    hb_night_set(&n, 1);
    quiet = run(&n, -45, 800);
    CHECK(quiet > -40, "dialogue (-45 dBFS) comes up by more than 5 dB");
    CHECK(loud - quiet < 42 - 12, "range between them is squeezed by more than 12 dB");
    printf("     quiet %.1f dB, loud %.1f dB, gain %d\n", quiet, loud, hb_night_gain_db10(&n));
    CHECK(hb_night_gain_db10(&n) > 50, "status: gain shows the lift (tenths of dB)");

    /* a sudden peak right after quiet: never above -1 dBFS */
    {
        double p = run(&n, 0, 2);
        int pk = 0;
        for (i = 0; i < RATE * 2 / 1000; i++) { int v = buf[2 * i] < 0 ? -buf[2 * i] : buf[2 * i]; if (v > pk) pk = v; }
        (void)p;
        CHECK(pk <= (int)(HB_NIGHT_CEIL * 32767) + 1, "sudden peak held under -1 dBFS");
    }

    memset(z, 0, sizeof z);
    hb_night_process(&n, z, 32);
    for (i = 0; i < 64 && !z[i]; i++) { }
    CHECK(i == 64, "silence stays silent");

    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
