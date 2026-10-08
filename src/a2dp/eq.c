/* Developed by X-F1REBALL-X. */
#include "eq.h"

#include <string.h>

const int hb_eq_hz[HB_EQ_NB] = { 80, 250, 1000, 3500, 10000 };
static const float g_q[HB_EQ_NB] = { 0.707f, 1.0f, 1.0f, 1.0f, 0.707f };

#define PI 3.14159265358979323846

double hb_sin(double x)
{
    double x2, r;
    int neg = 0;
    /* reduce to [-pi, pi], then [0, pi/2] */
    x -= (double)(long)(x / (2 * PI)) * (2 * PI);
    if (x > PI) x -= 2 * PI;
    if (x < -PI) x += 2 * PI;
    if (x < 0) { x = -x; neg = 1; }
    if (x > PI / 2) x = PI - x;
    x2 = x * x;
    r = x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110 * (1 - x2 / 156))))));
    return neg ? -r : r;
}

double hb_cos(double x) { return hb_sin(x + PI / 2); }

static double hb_exp(double x)
{
    /* e^x = 2^k * e^r, |r| <= ln2/2 */
    const double ln2 = 0.69314718055994530942;
    long k = (long)(x / ln2 + (x < 0 ? -0.5 : 0.5));
    double r = x - (double)k * ln2, t = 1, s = 1;
    int i;
    for (i = 1; i < 14; i++) { t *= r / i; s += t; }
    while (k > 0) { s *= 2; k--; }
    while (k < 0) { s *= 0.5; k++; }
    return s;
}

double hb_pow10(double x) { return hb_exp(x * 2.30258509299404568402); }

double hb_sqrt(double x)
{
    double g = x > 1 ? x : 1;
    int i;
    if (x <= 0) return 0;
    for (i = 0; i < 40; i++) g = 0.5 * (g + x / g);
    return g;
}

double hb_log10(double x)
{
    /* ln(x) = 2 atanh((x-1)/(x+1)) after scaling into [0.5, 2] */
    double y, y2, s = 0, t;
    int k = 0, i;
    if (x <= 0) return -300;
    while (x > 2) { x *= 0.5; k++; }
    while (x < 0.5) { x *= 2; k--; }
    y = (x - 1) / (x + 1);
    y2 = y * y;
    t = y;
    for (i = 1; i < 40; i += 2) { s += t / i; t *= y2; }
    return (2 * s + k * 0.69314718055994530942) / 2.30258509299404568402;
}

void hb_eq_init(hb_eq *e)
{
    memset(e, 0, sizeof *e);
    e->preamp = 1.f;
}

/* RBJ cookbook: peaking EQ, low shelf, high shelf (shelf slope S = 1). */
static void design(hb_biquad *q, int band, double gain_db, double fs)
{
    double A = hb_pow10(gain_db / 40.0), w0 = 2 * PI * hb_eq_hz[band] / fs;
    double cw = hb_cos(w0), sw = hb_sin(w0), alpha, b0, b1, b2, a0, a1, a2;
    if (band == 0 || band == HB_EQ_NB - 1) {
        double sa = 2 * hb_sqrt(A) * (sw / (2 * g_q[band]));
        if (band == 0) {
            b0 = A * ((A + 1) - (A - 1) * cw + sa);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sa);
            a0 = (A + 1) + (A - 1) * cw + sa;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sa;
        } else {
            b0 = A * ((A + 1) + (A - 1) * cw + sa);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sa);
            a0 = (A + 1) - (A - 1) * cw + sa;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sa;
        }
    } else {
        alpha = sw / (2 * g_q[band]);
        b0 = 1 + alpha * A;
        b1 = -2 * cw;
        b2 = 1 - alpha * A;
        a0 = 1 + alpha / A;
        a1 = -2 * cw;
        a2 = 1 - alpha / A;
    }
    q->b0 = (float)(b0 / a0); q->b1 = (float)(b1 / a0); q->b2 = (float)(b2 / a0);
    q->a1 = (float)(a1 / a0); q->a2 = (float)(a2 / a0);
}

void hb_eq_set(hb_eq *e, int on, const int db[HB_EQ_NB], int sample_rate)
{
    int i, maxb = 0, any = 0;
    for (i = 0; i < HB_EQ_NB; i++) {
        int d = db[i] < -12 ? -12 : db[i] > 12 ? 12 : db[i];
        e->db[i] = d;
        e->active[i] = on && d != 0;
        if (e->active[i]) {
            any = 1;
            design(&e->bq[i], i, d, sample_rate > 0 ? sample_rate : 48000);
        } else {
            memset(e->z[i], 0, sizeof e->z[i]);
        }
        if (on && d > maxb) maxb = d;
    }
    e->on = on && any;
    /* headroom: minus the largest boost (a little less for shelves stacking
     * is not needed: peaks of different bands sit at different frequencies) */
    e->preamp = (float)hb_pow10(-maxb / 20.0);
}

void hb_eq_process(hb_eq *e, float *lr, int frames)
{
    int b, i, c;
    if (!e->on) return;
    for (b = 0; b < HB_EQ_NB; b++) {
        const hb_biquad q = e->bq[b];
        if (!e->active[b]) continue;
        for (c = 0; c < 2; c++) {
            float z1 = e->z[b][c][0], z2 = e->z[b][c][1];
            float *p = lr + c;
            for (i = 0; i < frames; i++, p += 2) {
                float x = *p, y = q.b0 * x + z1;
                z1 = q.b1 * x - q.a1 * y + z2;
                z2 = q.b2 * x - q.a2 * y;
                *p = y;
            }
            /* flush denormals so silence stays cheap */
            if (z1 < 1e-20f && z1 > -1e-20f) z1 = 0;
            if (z2 < 1e-20f && z2 > -1e-20f) z2 = 0;
            e->z[b][c][0] = z1;
            e->z[b][c][1] = z2;
        }
    }
}

float hb_eq_response_db(const hb_eq *e, float hz, int sample_rate)
{
    double w = 2 * PI * hz / (sample_rate > 0 ? sample_rate : 48000), total = 0;
    double c1 = hb_cos(w), s1 = hb_sin(w), c2 = hb_cos(2 * w), s2 = hb_sin(2 * w);
    int b;
    if (!e->on) return 0.f;
    for (b = 0; b < HB_EQ_NB; b++) {
        const hb_biquad *q = &e->bq[b];
        double nr, ni, dr, di;
        if (!e->active[b]) continue;
        nr = q->b0 + q->b1 * c1 + q->b2 * c2;
        ni = -(q->b1 * s1 + q->b2 * s2);
        dr = 1 + q->a1 * c1 + q->a2 * c2;
        di = -(q->a1 * s1 + q->a2 * s2);
        total += 10 * hb_log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    return (float)total;
}
