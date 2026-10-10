/* USB ACL transmit path decisions, pure so the host test can drive them.
 *  - A Disconnection Complete frees the handle's controller buffers: frames
 *    still queued for that handle are dropped, not sent into a dead link.
 *  - The spare bulk OUT pipe only covers a stalled primary. If the
 *    controller completes nothing sent on it (no Number Of Completed
 *    Packets at all), it is not an ACL pipe on this chip (Marvell 1286:2059
 *    0x02): go back to the primary and do not use the spare again.
 *  - Any disconnect also puts the transmitter back on the primary, so a
 *    new link never starts on the spare.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_TXPATH_H
#define HEARBRIDGE_TXPATH_H

#define HB_SPARE_PROVE_MS 1000   /* spare must see a completion within this */

/* ACL handle of a raw ACL frame (no H4 byte), -1 if too short. */
int hb_acl_frame_handle(const unsigned char *f, int n);
/* Handle of a successful Disconnection Complete event (code, len, params),
 * -1 for any other event. */
int hb_evt_disc_handle(const unsigned char *ev, int n);
/* 1 for a Number Of Completed Packets event that frees at least one packet. */
int hb_evt_is_nocp(const unsigned char *ev, int n);

typedef struct {
    int  on_spare;               /* transmitting on the spare pipe */
    int  spare_bad;              /* spare proved useless: never again */
    long t_switch;               /* when we moved onto the spare */
    unsigned long sent_spare;    /* frames completed by USB on the spare */
    unsigned long nocp_spare;    /* completion events seen since */
} hb_txpath;

void hb_txpath_init(hb_txpath *t);
/* Primary stalled: 1 = move to the spare now (it exists and is not bad). */
int  hb_txpath_stall(hb_txpath *t, int spare_open, long now);
void hb_txpath_sent(hb_txpath *t);
void hb_txpath_nocp(hb_txpath *t);
/* 1 = go back to the primary now (spare never completed anything);
 * marks the spare bad. */
int  hb_txpath_check(hb_txpath *t, long now);
/* A handle disconnected: 1 = go back to the primary (we were on the spare). */
int  hb_txpath_disc(hb_txpath *t);

#endif
