/* Developed by X-F1REBALL-X.
 * No libm here: the payload imports only what 1.0.2 did, so log2 / exp2 are
 * small bit-level approximations (well under 0.1 dB off, plenty for this). */
#include "night.h"

#include <stdint.h>
#include <string.h>

#define DB_PER_LOG2 6.0205999f   /* 20 * log10(2) */

static float absf(float x) { return x < 0.f ? -x : x; }

static float log2_fast(float x)
{
    union { float f; uint32_t i; } u = { x };
    float e = (float)(int)((u.i >> 23) & 255) - 127.f, m;
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;   /* mantissa in [1,2) */
    m = u.f;
    return e + (((0.15824871f * m - 1.05187502f) * m + 3.04788335f) * m - 2.15468851f);
}

static float exp2_fast(float x)
{
    union { float f; uint32_t i; } u;
    int n;
    float f;
    if (x < -126.f) return 0.f;
    if (x > 126.f) x = 126.f;
    n = (int)x;
    if ((float)n > x) n--;                       /* floor */
    f = x - (float)n;
    u.f = 1.f + f * (0.6951786f + f * (0.2261204f + f * 0.0787010f));   /* p(0)=1, p(1)=2 */
    u.i += (uint32_t)n << 23;
    return u.f;
}

static float coef(float ms, int rate)
{
    /* 1 - e^(-1/(ms*rate/1000)) */
    return 1.f - exp2_fast(-1.4426950f * 1000.f / (ms * (float)rate));
}

void hb_night_init(hb_night *n, int rate)
{
    memset(n, 0, sizeof *n);
    if (rate <= 0) rate = 48000;
    n->a_env = coef(2.f, rate);     /* level: fast up */
    n->r_env = coef(120.f, rate);   /*        slower down */
    n->a_g = coef(4.f, rate);       /* gain: duck in a few ms */
    n->r_g = coef(300.f, rate);     /*       come back gently (no pumping) */
    n->gain = 1.f;
}

void hb_night_set(hb_night *n, int on)
{
    on = on != 0;
    if (on && !n->on) { n->env = 0.f; n->gain = 1.f; }
    n->on = on;
}

float hb_night_curve_db(float l)
{
    float g = HB_NIGHT_MAKEUP_DB, fade = HB_NIGHT_FLOOR_DB - 8.f;
    if (l > HB_NIGHT_THRESH_DB) g -= (l - HB_NIGHT_THRESH_DB) * (1.f - 1.f / HB_NIGHT_RATIO);
    if (l < HB_NIGHT_FLOOR_DB) {
        /* fade the lift out over 8 dB under the floor */
        float k = l <= fade ? 0.f : (l - fade) / (HB_NIGHT_FLOOR_DB - fade);
        g *= k;
    }
    return g;
}

static int16_t to_s16(float v)
{
    v *= 32767.f;
    v += v >= 0.f ? .5f : -.5f;
    if (v > 32767.f) return 32767;
    if (v < -32768.f) return -32768;
    return (int16_t)(int)v;
}

void hb_night_process(hb_night *n, int16_t *pcm, int frames)
{
    int i;
    if (!n->on) return;
    for (i = 0; i < frames; i++) {
        float l = pcm[2 * i] / 32768.f, r = pcm[2 * i + 1] / 32768.f;
        float a = absf(l) > absf(r) ? absf(l) : absf(r);
        float target, db, ol, or_, m;
        n->env += (a > n->env ? n->a_env : n->r_env) * (a - n->env);
        db = n->env > 1e-6f ? DB_PER_LOG2 * log2_fast(n->env) : -120.f;
        target = exp2_fast(hb_night_curve_db(db) / DB_PER_LOG2);
        n->gain += (target < n->gain ? n->a_g : n->r_g) * (target - n->gain);
        ol = l * n->gain;
        or_ = r * n->gain;
        /* peaks the follower has not caught yet: hold them under -1 dBFS */
        m = absf(ol) > absf(or_) ? absf(ol) : absf(or_);
        if (m > HB_NIGHT_CEIL) {
            ol *= HB_NIGHT_CEIL / m;
            or_ *= HB_NIGHT_CEIL / m;
        }
        pcm[2 * i] = to_s16(ol);
        pcm[2 * i + 1] = to_s16(or_);
    }
}

int hb_night_gain_db10(const hb_night *n)
{
    float d;
    if (!n->on || n->gain <= 0.f) return 0;
    d = 10.f * DB_PER_LOG2 * log2_fast(n->gain);
    return (int)(d + (d >= 0.f ? .5f : -.5f));
}
