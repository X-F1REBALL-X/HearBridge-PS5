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

int gain_apply_soft(int16_t *pcm, int samples, int gain_milli)
{
    const float t = HB_LIMIT_KNEE, r = 1.f - HB_LIMIT_KNEE;
    float g = (float)gain_milli / 1000.f, peak = 0.f;
    int i;
    for (i = 0; i < samples; i++) {
        float y = (float)pcm[i] / 32768.f * g;
        float a = y < 0 ? -y : y;
        if (a > t) {
            float u = (a - t) / r;
            a = t + r * (u / (1.f + u));     /* approaches 1.0, never reaches */
        }
        if (a > peak) peak = a;
        y = y < 0 ? -a : a;
        pcm[i] = (int16_t)(y * 32767.f);
    }
    return (int)(peak * 1000.f + 0.5f);
}

static float soft(float y, float *peak)
{
    const float t = HB_LIMIT_KNEE, r = 1.f - HB_LIMIT_KNEE;
    float a = y < 0 ? -y : y;
    if (a > t) {
        float u = (a - t) / r;
        a = t + r * (u / (1.f + u));
    }
    if (a > *peak) *peak = a;
    return y < 0 ? -a : a;
}

int gain_apply_soft_eq(int16_t *pcm, int samples, int gain_milli, void *eqp)
{
    hb_eq *eq = (hb_eq *)eqp;
    float buf[2048], peak = 0.f, g;
    int off = 0, i;

    if (!eq || !eq->on) return gain_apply_soft(pcm, samples, gain_milli);
    g = (float)gain_milli / 1000.f * eq->preamp / 32768.f;
    while (off < samples) {
        int n = samples - off;
        if (n > (int)(sizeof buf / sizeof buf[0])) n = (int)(sizeof buf / sizeof buf[0]);
        n &= ~1;
        if (n <= 0) break;
        for (i = 0; i < n; i++) buf[i] = (float)pcm[off + i] * g;
        hb_eq_process(eq, buf, n / 2);
        for (i = 0; i < n; i++) pcm[off + i] = (int16_t)(soft(buf[i], &peak) * 32767.f);
        off += n;
    }
    return (int)(peak * 1000.f + 0.5f);
}
