/* Developed by X-F1REBALL-X.
 * Codec switch / reconnect steps, pure logic (host-tested):
 *   1. in place: CLOSE + SET_CONFIGURATION + OPEN + START on the same link
 *   2. if that fails but the link is still up: in place again with plain SBC
 *   3. if the link is gone (or SBC failed too): page the headset again,
 *      HB_CS_TRIES attempts with growing pauses, then give up (idle).
 * The auto SBC-XQ -> SBC fallback goes through the same steps. */
#ifndef HB_CSWITCH_H
#define HB_CSWITCH_H

#define HB_CS_IDLE        0
#define HB_CS_INPLACE     1   /* switch to want on the open link */
#define HB_CS_INPLACE_SBC 2   /* plain SBC on the open link */
#define HB_CS_RECONNECT   3   /* page the headset (after delay_ms) */
#define HB_CS_DONE        4   /* streaming again */
#define HB_CS_GIVEUP      5   /* stay idle, the user presses Connect */

#define HB_CS_TRIES       4

typedef struct {
    int step;        /* HB_CS_* */
    int want;        /* HB_CODEC_* asked for */
    int no_xq;       /* auto must not pick SBC-XQ */
    int attempt;     /* reconnect attempts made */
    long delay_ms;   /* wait before the next reconnect */
} hb_cswitch;

void hb_cs_begin(hb_cswitch *c, int want, int no_xq);
/* Result of the current step (ok = it worked, link_up = the ACL and
 * signalling are still there). Returns the next step. */
int  hb_cs_next(hb_cswitch *c, int ok, int link_up);
/* Pause before reconnect attempt n (0-based). */
long hb_cs_delay(int attempt);

#endif
