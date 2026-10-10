/* Developed by X-F1REBALL-X. See linktune.h. */
#include "linktune.h"

#define LONG_AGO (-1000000000L)

void hb_tune_init(hb_tune *t)
{
    t->max_pp = 0;
    t->jitter_s = 0;
    t->t_drop = LONG_AGO;
    t->t_change = LONG_AGO;
    t->freeze_until = LONG_AGO;
}

int hb_tune_frozen(const hb_tune *t, long now)
{
    return now < t->freeze_until;
}

static int ceil_for(int pp, int fit)
{
    return pp >= fit ? 0 : pp;           /* at the fit: no ceiling */
}

int hb_tune_step(hb_tune *t, const hb_tune_in *in)
{
    long now = in->now;
    int calm_before = now - t->t_drop > 3000;    /* no drop in the last 3 s */
    if (in->new_drops > 0) t->t_drop = now;

    /* A dip: drops with the queue half full right after a calm stretch. */
    if (in->new_drops > 0 && calm_before && in->backlog * 2 > in->cap && !hb_tune_frozen(t, now)) {
        t->freeze_until = now + HB_TUNE_FREEZE_MS;
        t->jitter_s = 0;
        return HB_TUNE_DIP;
    }
    if (hb_tune_frozen(t, now)) return HB_TUNE_KEEP;

    if (in->new_drops > 0) {
        if (in->cred_pps >= in->need_pps * 1.05) {
            /* enough credits on average: bursty return, smaller packets */
            if (++t->jitter_s >= 2 && in->per_pkt > HB_TUNE_MIN_PP) {
                int pp = in->per_pkt - 2 < HB_TUNE_MIN_PP ? HB_TUNE_MIN_PP : in->per_pkt - 2;
                t->max_pp = ceil_for(pp, in->fit_pp);
                t->jitter_s = 0;
                t->t_change = now;
                return HB_TUNE_SHRINK;
            }
            return HB_TUNE_KEEP;
        }
        t->jitter_s = 0;
        if (in->cred_pps < in->need_pps && in->per_pkt < in->fit_pp) {
            /* the link is slow: fewer, bigger packets */
            int pp = in->per_pkt + 2;
            t->max_pp = ceil_for(pp, in->fit_pp);
            t->t_change = now;
            return HB_TUNE_GROW;
        }
        return HB_TUNE_KEEP;
    }
    t->jitter_s = 0;
    if (t->max_pp > 0 && now - t->t_drop >= HB_TUNE_RECOVER_MS && now - t->t_change >= HB_TUNE_RECOVER_MS &&
        in->cred_pps > in->need_pps * 1.2) {
        t->max_pp = ceil_for(t->max_pp + 2, in->fit_pp);
        t->t_change = now;
        return HB_TUNE_RECOVER;
    }
    return HB_TUNE_KEEP;
}

const char *hb_tune_name(int a)
{
    switch (a) {
    case HB_TUNE_SHRINK:  return "smaller packets (bursty credits)";
    case HB_TUNE_GROW:    return "bigger packets (slow link)";
    case HB_TUNE_RECOVER: return "packets grow back";
    case HB_TUNE_DIP:     return "dip: bitpool at its floor, packet size frozen";
    default:              return "keep";
    }
}
