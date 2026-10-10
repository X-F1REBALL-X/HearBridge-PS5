/* Developed by X-F1REBALL-X.
 * Every ACL on the controller, whoever opened it: fed with each HCI event
 * as it arrives from the transport, so a link the headset opened by itself
 * (inbound) is known by handle even if no one was reading at the time. */
#ifndef HB_ACL_TRACK_H
#define HB_ACL_TRACK_H

void acl_track_event(const unsigned char *ev, int n, long now_ms);
/* Handle of a live ACL to addr, 0 if none known. */
unsigned acl_track_handle(const unsigned char addr[6]);
/* Handle of a live ACL to addr whoever opened it (the console's own stack
 * included), 0 if none known or that handle connected / disconnected again
 * since. Only for closing a saved headset's link held elsewhere. */
unsigned acl_track_any_handle(const unsigned char addr[6]);
/* ms since a Connection Request from addr that has not completed, -1 if none. */
long acl_track_request_age(const unsigned char addr[6], long now_ms);
/* The request was answered (accepted, rejected or given up on). */
void acl_track_request_clear(const unsigned char addr[6]);
/* A request not answered by the host stays pending at the controller until
 * its Connection Accept Timeout (seen ~25 s on the PS5 radio) and blocks our
 * page to that address with 0x0b meanwhile. Younger than this: still worth
 * answering. */
#define ACL_REQ_PENDING_MS 20000
/* Changes whenever the handle connects or disconnects (any reader). A handle
 * remembered with an older epoch is gone, maybe reused by a pad. */
unsigned acl_track_handle_epoch(unsigned handle);

/* Last page scan repetition mode and clock offset (bit15 = valid) seen for
 * addr from inquiry results or Read Clock Offset. 1 if any is known. */
int acl_track_page_params(const unsigned char addr[6], unsigned char *psrm, unsigned *clock);

#endif
