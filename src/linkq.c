/* Developed by X-F1REBALL-X. */
#include "linkq.h"

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void hb_linkq_init(hb_linkq *q)
{
    q->last_drops = 0;
    q->t_last = 0;
    q->drops_x10 = 0;
}

int hb_linkq_drops_per_min(hb_linkq *q, long dropped, long now)
{
    long d, dt;
    int inst;
    if (!q->t_last || dropped < q->last_drops) {     /* first sample / new link */
        q->t_last = now;
        q->last_drops = dropped;
        q->drops_x10 = 0;
        return 0;
    }
    dt = now - q->t_last;
    if (dt < 200) return q->drops_x10 / 10;
    d = dropped - q->last_drops;
    inst = (int)(d * 600000L / dt);                  /* per minute x10 */
    if (inst > 99990) inst = 99990;
    /* smooth over ~10 s, but jump up at once so a burst shows */
    q->drops_x10 = inst > q->drops_x10 ? inst : q->drops_x10 + (inst - q->drops_x10) / 10;
    q->t_last = now;
    q->last_drops = dropped;
    return q->drops_x10 / 10;
}

int hb_linkq_rssi_score(int rssi)
{
    if (rssi == HB_RSSI_UNKNOWN || rssi > 20) return -1;
    if (rssi < -20) {                       /* absolute dBm: -50 great, -90 gone */
        return clampi((rssi + 90) * 100 / 40, 0, 100);
    }
    if (rssi >= 0) return 100;              /* inside (or above) the golden range */
    return clampi(100 + rssi * 4, 0, 100);  /* -5 = 80, -10 = 60, -20 = 20 */
}

int hb_linkq_score(int rssi, int lq, int dpm, int backlog, int cap)
{
    int rs = hb_linkq_rssi_score(rssi), radio = -1, s, pen = 0;
    if (rs >= 0 && lq >= 0) radio = (rs + lq * 100 / 255) / 2;
    else if (rs >= 0) radio = rs;
    else if (lq >= 0) radio = lq * 100 / 255;
    /* what the stream sees counts for real: drops hurt more than a weak number */
    if (dpm > 0) pen += dpm >= 30 ? 60 : 10 + dpm * 50 / 30;
    if (cap > 0 && backlog > 0) pen += clampi(backlog * 30 / cap, 0, 30);
    if (radio < 0) {
        if (dpm < 0) return -1;
        radio = 100;                        /* no radio numbers: stream only */
    }
    s = radio - pen;
    return clampi(s, 0, 100);
}

int hb_linkq_bars(int score)
{
    if (score < 0) return 0;
    return score >= 80 ? 4 : score >= 55 ? 3 : score >= 30 ? 2 : 1;
}
