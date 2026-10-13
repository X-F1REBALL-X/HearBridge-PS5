/* Developed by X-F1REBALL-X. */
#include "connreq.h"

#include <string.h>

/* #34 allowlist: only Audio/Video major class AND a headphone / headset /
 * speaker minor class. Everything else (DualSense / DS4 = Peripheral 0x05
 * gamepad, phones, PCs, TVs) is never touched: left to the system. */
int hb_cod_is_av(unsigned cod)
{
    unsigned major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;
    if (major != 0x04) return 0;
    switch (minor) {
    case 0x01: case 0x02: case 0x05: case 0x06: case 0x07: case 0x0A: return 1;
    default: return 0;
    }
}

int hb_connreq_decide(const hb_cr_in *in)
{
    if (!in) return HB_CR_LEAVE;
    if (in->blocked) return HB_CR_LEAVE;               /* #34: blocklist, always the system's */
    /* #34: not an audio sink -> never ours, whatever the saved list or the
     * current target says (a pad must always reach the system). */
    if (!in->is_av) return HB_CR_LEAVE;
    if (in->is_target) return HB_CR_TAKE;             /* the one the user pressed */
    if (in->forgotten) return HB_CR_REJECT_UNKNOWN;
    if (in->saved) {
        /* Just left (switch / Disconnect) and calling back: a clean accept
         * and disconnect, never a busy rejection it may sulk about. */
        if (in->just_dropped) return HB_CR_ACCEPT_DROP;
        if (in->held) return HB_CR_REJECT_BUSY;       /* disconnected by hand */
        if (in->streaming_other) return HB_CR_SWITCH; /* taken out of its case: newest wins */
        /* Out of its case while we only re-page another one in the
         * background: it wins (that page stops for it). */
        if (in->busy_other && !in->bg_page) return HB_CR_REJECT_BUSY;
        return HB_CR_TAKE;
    }
    return HB_CR_REJECT_UNKNOWN;                      /* audio device we do not know */
}

const char *hb_connreq_name(int d)
{
    switch (d) {
    case HB_CR_TAKE:           return "accepting";
    case HB_CR_SWITCH:         return "switching to it";
    case HB_CR_REJECT_BUSY:    return "rejecting (busy, 0x0D)";
    case HB_CR_REJECT_UNKNOWN: return "rejecting (not saved, 0x0F)";
    case HB_CR_ACCEPT_DROP:    return "accepting, then closing it cleanly (just left)";
    default:                   return "left to the system";
    }
}

void hb_dropped_note(hb_dropped *d, const unsigned char addr[6], long now)
{
    memcpy(d->addr, addr, 6);
    d->t = now;
    d->on = 1;
}

int hb_dropped_recent(const hb_dropped *d, const unsigned char addr[6], long now, long within_ms)
{
    return d->on && !memcmp(d->addr, addr, 6) && now - d->t >= 0 && now - d->t < within_ms;
}

int hb_drop_is_away(int reason)
{
    return reason == 0x13;
}
