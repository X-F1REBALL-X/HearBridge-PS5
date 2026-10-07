/* Small SDP server for the records HearBridge offers to the headset:
 * A2DP Source, AVRCP Target (Category 2, absolute volume) and AVRCP
 * Controller (Category 2). Pure byte-in/byte-out, host testable.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_SDP_SERVER_H
#define HEARBRIDGE_SDP_SERVER_H

/* Handle one SDP request PDU; writes the response PDU. Returns its length,
 * or 0 if nothing should be sent. rsp_max should be >= the channel MTU. */
int sdp_server_handle(const unsigned char *req, int len,
                      unsigned char *rsp, int rsp_max);

#endif
