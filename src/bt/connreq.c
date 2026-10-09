/* Developed by X-F1REBALL-X. */
#include "connreq.h"

int hb_cod_is_av(unsigned cod)
{
    return ((cod >> 8) & 0x1F) == 0x04;
}

int hb_connreq_decide(const hb_cr_in *in)
{
    if (!in) return HB_CR_LEAVE;
    if (in->is_target) return HB_CR_TAKE;             /* the one the user pressed */
    if (in->forgotten) return HB_CR_REJECT_UNKNOWN;
    if (in->saved) {
        if (in->held) return HB_CR_REJECT_BUSY;       /* disconnected by hand */
        if (in->streaming_other) return HB_CR_SWITCH; /* taken out of its case: newest wins */
        /* Out of its case while we only re-page another one in the
         * background: it wins (that page stops for it). */
        if (in->busy_other && !in->bg_page) return HB_CR_REJECT_BUSY;
        return HB_CR_TAKE;
    }
    if (!in->is_av) return HB_CR_LEAVE;               /* pads, phones, PCs */
    return HB_CR_REJECT_UNKNOWN;                      /* audio device we do not know */
}

const char *hb_connreq_name(int d)
{
    switch (d) {
    case HB_CR_TAKE:           return "accepting";
    case HB_CR_SWITCH:         return "switching to it";
    case HB_CR_REJECT_BUSY:    return "rejecting (busy, 0x0D)";
    case HB_CR_REJECT_UNKNOWN: return "rejecting (not saved, 0x0F)";
    default:                   return "left to the system";
    }
}
