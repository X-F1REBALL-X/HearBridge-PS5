/* Developed by X-F1REBALL-X. */
#include "gain.h"
#include "ctl.h"
#include "eq.h"

#include <stdio.h>

int gain_parse_pct(const char *p)
{
    long whole = 0, frac = 0, fdiv = 1;
    int any = 0;
    if (!p) return -1;
    while (*p == ' ' || *p == '\t') p++;
    for (; *p >= '0' && *p <= '9'; p++) { whole = whole * 10 + (*p - '0'); any = 1; if (whole > 1000) break; }
    if (*p == '.') {
        for (p++; *p >= '0' && *p <= '9' && fdiv < 1000; p++) {
            frac = frac * 10 + (*p - '0'); fdiv *= 10; any = 1;
        }
    }
    if (!any) return -1;
    {
        long pct = whole * 100 + (frac * 100 + fdiv / 2) / fdiv;
        if (pct < 0) pct = 0;
        if (pct > HB_GAIN_MAX_PCT) pct = HB_GAIN_MAX_PCT;
        return (int)pct;
    }
}

void gain_format(int pct, char *out, int max)
{
    snprintf(out, (size_t)max, "%d.%02d\n", pct / 100, pct % 100);
}

/* Limiter: stereo-linked envelope with a soft knee. The wanted output
 * level for an input level a is the old soft curve f(a) (linear up to the
 * knee, then bending towards 1.0 and never reaching it); the gain needed
 * for that, f(a)/a, is applied at once when it is lower (instant attack,
 * so no sample can go over) and comes back to 1 with a ~100 ms release.
 * Unlike a per-sample curve, the waveform is scaled, not bent: loud
 * passages get quieter instead of distorted, so more gain is usable. */
static float g_env = 1.f;
static int g_hold;
#define LIM_REL  0.99979167f   /* 1 - 1/(0.1 s * 48000): ~100 ms at 48 kHz */
#define LIM_HOLD 480           /* 10 ms hold before releasing: no ripple inside a cycle */

void gain_limiter_reset(void) { g_env = 1.f; g_hold = 0; }

static float knee_gain(float a)
{
    const float t = HB_LIMIT_KNEE, r = 1.f - HB_LIMIT_KNEE;
    float u;
    if (a <= t) return 1.f;
    u = (a - t) / r;
    return (t + r * (u / (1.f + u))) / a;
}

/* One frame (l, r already scaled by the gain), in place; tracks the peak. */
static void limit_frame(float *l, float *r, float *peak)
{
    float al = *l < 0 ? -*l : *l, ar = r ? (*r < 0 ? -*r : *r) : 0.f;
    float a = al > ar ? al : ar, want = knee_gain(a), o;
    if (want < g_env) { g_env = want; g_hold = LIM_HOLD; }   /* instant attack */
    else if (want < 1.f && want <= g_env * 1.02f) g_hold = LIM_HOLD; /* still at the ceiling */
    else if (g_hold > 0) g_hold--;
    else {
        g_env = 1.f - (1.f - g_env) * LIM_REL;        /* release towards 1 */
        if (want < g_env) g_env = want;
    }
    *l *= g_env;
    if (r) *r *= g_env;
    o = a * g_env;
    if (o > *peak) *peak = o;
}

static int16_t to16(float y)
{
    float v = y * 32767.f;
    if (v > 32767.f) v = 32767.f;
    if (v < -32767.f) v = -32767.f;
    return (int16_t)v;
}

int gain_apply_soft(int16_t *pcm, int samples, int gain_milli)
{
    float g = (float)gain_milli / 1000.f / 32768.f, peak = 0.f;
    int i;
    for (i = 0; i + 1 < samples; i += 2) {
        float l = (float)pcm[i] * g, r = (float)pcm[i + 1] * g;
        limit_frame(&l, &r, &peak);
        pcm[i] = to16(l);
        pcm[i + 1] = to16(r);
    }
    if (i < samples) {
        float l = (float)pcm[i] * g;
        limit_frame(&l, NULL, &peak);
        pcm[i] = to16(l);
    }
    return (int)(peak * 1000.f + 0.5f);
}

/* EQ first (no preamp: the limiter takes care of the headroom), then the
 * gain, then the limiter. */
int gain_apply_soft_eq(int16_t *pcm, int samples, int gain_milli, void *eqp)
{
    hb_eq *eq = (hb_eq *)eqp;
    float buf[2048], peak = 0.f, g;
    int off = 0, i;

    if (!eq || !eq->on) return gain_apply_soft(pcm, samples, gain_milli);
    g = (float)gain_milli / 1000.f;
    while (off < samples) {
        int n = samples - off;
        if (n > (int)(sizeof buf / sizeof buf[0])) n = (int)(sizeof buf / sizeof buf[0]);
        n &= ~1;
        if (n <= 0) break;
        for (i = 0; i < n; i++) buf[i] = (float)pcm[off + i] / 32768.f;
        hb_eq_process(eq, buf, n / 2);
        for (i = 0; i < n; i += 2) {
            float l = buf[i] * g, r = buf[i + 1] * g;
            limit_frame(&l, &r, &peak);
            pcm[off + i] = to16(l);
            pcm[off + i + 1] = to16(r);
        }
        off += n;
    }
    return (int)(peak * 1000.f + 0.5f);
}
