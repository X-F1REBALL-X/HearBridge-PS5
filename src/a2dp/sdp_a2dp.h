/* SDP client: find A2DP Sink + AVDTP L2CAP PSM. */
#ifndef HEARBRIDGE_SDP_A2DP_H
#define HEARBRIDGE_SDP_A2DP_H

#include "btlink.h"

/* Returns AVDTP PSM (usually 0x0019). On SDP open/config failure,
 * falls back to assigned PSM 0x0019 (never returns 0 if link is up). */
unsigned sdp_find_avdtp_psm(btlink *link, int timeout_ms);

/* Strict A2DP Sink check (ServiceSearchAttribute for UUID 0x110B).
 * Returns 1 = sink advertised (*psm set), 0 = SDP answered with no sink
 * record, -1 = SDP unreachable (inconclusive; *psm = 0x0019 fallback). */
int sdp_probe_a2dp_sink(btlink *link, int timeout_ms, unsigned *psm);

/* PSM to try AVDTP on: the one SDP found, else the assigned 0x0019 (an
 * SDP that never ran, e.g. the link dropped mid-search, gives 0). */
static inline unsigned sdp_avdtp_psm_or_default(unsigned psm)
{
    return psm ? psm : BTLINK_PSM_AVDTP;
}

/* Set: one SDP attempt only (timeout_ms is the whole budget). */
extern int sdp_one_round;

#endif
