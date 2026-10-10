/* Developed by X-F1REBALL-X.
 * Frames per media packet from what the link really delivers, once a
 * second. Pure logic (no I/O) so the host test can replay a slow link.
 *
 *  - Credits come back fast enough on average but packets still drop
 *    (bursty credit return): smaller packets, each credit is held for less.
 *  - Credits come back slower than packets are needed: bigger packets
 *    (fewer per second), up to the MTU fit. Shrinking here only raised the
 *    packets needed per second and locked in ~45 drops/s (hb7.log, OnePlus
 *    Buds Ace 2: 13 credits/s for 34 packets/s, 34 -> 42 -> 56 -> 75).
 *  - A sudden dip (drops with the queue half full after a calm stretch):
 *    hold the bitpool at its floor and change no packet size for
 *    HB_TUNE_FREEZE_MS, so a short dip is not locked in.
 *  - 10 s without a new drop and credits with 20 % headroom: let the
 *    ceiling grow back, 2 frames per step. */
#ifndef HB_LINKTUNE_H
#define HB_LINKTUNE_H

#define HB_TUNE_FREEZE_MS   5000
#define HB_TUNE_RECOVER_MS 10000
#define HB_TUNE_MIN_PP         5

typedef struct {
    int max_pp;          /* frames/packet ceiling, 0 = MTU fit */
    int jitter_s;        /* seconds in a row with bursty drops */
    long t_drop;         /* last second with a new drop */
    long t_change;       /* last ceiling change */
    long freeze_until;   /* dip: no size change, bitpool at its floor */
} hb_tune;

typedef struct {
    long now;            /* ms */
    int per_pkt;         /* frames/packet in use */
    int fit_pp;          /* frames/packet the MTU (and latency target) allow */
    double cred_pps;     /* credits returned per second */
    double need_pps;     /* packets per second at per_pkt */
    int new_drops;       /* media packets dropped since the last step */
    int backlog, cap;    /* media queue now / its capacity */
} hb_tune_in;

enum { HB_TUNE_KEEP = 0, HB_TUNE_SHRINK, HB_TUNE_GROW, HB_TUNE_RECOVER, HB_TUNE_DIP };

void hb_tune_init(hb_tune *t);
/* One step (about once a second). Returns what changed; t->max_pp is the
 * new ceiling. HB_TUNE_DIP: hold the bitpool at its floor until
 * t->freeze_until. */
int  hb_tune_step(hb_tune *t, const hb_tune_in *in);
/* 1 while a dip freeze runs. */
int  hb_tune_frozen(const hb_tune *t, long now);
const char *hb_tune_name(int action);

#endif
