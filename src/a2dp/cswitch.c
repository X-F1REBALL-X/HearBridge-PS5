/* Developed by X-F1REBALL-X. */
#include "cswitch.h"
#include "hsprefs.h"

#include <string.h>

long hb_cs_delay(int attempt)
{
    static const long d[HB_CS_TRIES] = { 1000, 3000, 8000, 15000 };
    if (attempt < 0) attempt = 0;
    return d[attempt < HB_CS_TRIES ? attempt : HB_CS_TRIES - 1];
}

void hb_cs_begin(hb_cswitch *c, int want, int no_xq)
{
    memset(c, 0, sizeof *c);
    c->want = want;
    c->no_xq = no_xq;
    c->step = HB_CS_INPLACE;
}

static int reconnect(hb_cswitch *c)
{
    if (c->attempt >= HB_CS_TRIES) return c->step = HB_CS_GIVEUP;
    c->delay_ms = hb_cs_delay(c->attempt);
    c->attempt++;
    return c->step = HB_CS_RECONNECT;
}

int hb_cs_next(hb_cswitch *c, int ok, int link_up)
{
    switch (c->step) {
    case HB_CS_INPLACE:
        if (ok) return c->step = HB_CS_DONE;
        if (link_up && c->want != HB_CODEC_SBC) return c->step = HB_CS_INPLACE_SBC;
        return reconnect(c);
    case HB_CS_INPLACE_SBC:
        if (ok) return c->step = HB_CS_DONE;
        return reconnect(c);
    case HB_CS_RECONNECT:
        if (ok) return c->step = HB_CS_DONE;
        return reconnect(c);
    default:
        return c->step;
    }
}
