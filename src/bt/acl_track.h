/* Developed by X-F1REBALL-X.
 * Every ACL on the controller, whoever opened it: fed with each HCI event
 * as it arrives from the transport, so a link the headset opened by itself
 * (inbound) is known by handle even if no one was reading at the time. */
#ifndef HB_ACL_TRACK_H
#define HB_ACL_TRACK_H

void acl_track_event(const unsigned char *ev, int n, long now_ms);
/* Handle of a live ACL to addr, 0 if none known. */
unsigned acl_track_handle(const unsigned char addr[6]);
/* ms since a Connection Request from addr that has not completed, -1 if none. */
long acl_track_request_age(const unsigned char addr[6], long now_ms);

/* Last page scan repetition mode and clock offset (bit15 = valid) seen for
 * addr from inquiry results or Read Clock Offset. 1 if any is known. */
int acl_track_page_params(const unsigned char addr[6], unsigned char *psrm, unsigned *clock);

#endif
