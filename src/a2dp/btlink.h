/* Classic ACL + L2CAP for one headset link (headset-only; no HID roles).
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_BTLINK_H
#define HEARBRIDGE_BTLINK_H

#include "hci.h"

#include <stdint.h>

#define BTLINK_CID_SIGNALING 0x0001
#define BTLINK_PSM_SDP       0x0001
#define BTLINK_PSM_RFCOMM    0x0003  /* HFP AG, battery only */
#define BTLINK_PSM_AVDTP     0x0019
#define BTLINK_PSM_AVCTP     0x0017  /* AVRCP control */
#define BTLINK_PSM_AVCTP_BR  0x001B  /* AVRCP browsing */

#define BTLINK_CHAN_MAX 16
#define BTLINK_RX_MAX   1024

typedef struct btlink btlink;

typedef void (*btlink_rx_fn)(void *ud, unsigned scid,
                             const unsigned char *d, int len);

btlink *btlink_create(hci_t hci, int acl_mtu, int acl_buffers);
void    btlink_destroy(btlink *l);

/* Outbound Create Connection + auth/encrypt.
 * link_key NULL → Link Key Neg Reply (fresh pair).
 * Non-NULL → Link Key Reply (reconnect). Keeps ACL up on success.
 * On ACL-already-exists (0x0b): disconnect + retry once. */
int btlink_connect(btlink *l, const unsigned char addr[6],
                   unsigned char psrm, unsigned clock_offset,
                   const unsigned char *link_key, unsigned char key_type,
                   char *name_inout, int name_max, int timeout_ms);

int            btlink_is_up(const btlink *l);
unsigned       btlink_handle(const btlink *l);
const unsigned char *btlink_addr(const btlink *l);

void btlink_disconnect(btlink *l);
/* Last own-ACL close: 1 Disconnection Complete seen, 0 not seen, -1 none. */
int  btlink_last_close_confirmed(void);
/* 1 if an ACL this app created is still believed up. */
int  btlink_own_acl_pending(void);
/* HCI status of the last failed btlink_connect page (0x04 = page timeout),
 * 0 when it failed for another reason (auth, transport, 0x0b). */
int  btlink_last_connect_fail(void);
/* HCI reason of the last drop of a link this app owned (0x13 case / remote
 * user, 0x08 supervision timeout). 0 if none yet. */
int  btlink_last_disc_reason(void);
/* HCI Disconnect (0x13) one known handle and wait for Disconnection
 * Complete. Only for handles seen in Connection Complete events. */
/* Take over an encrypted ACL left up by a2dp_pair (fresh L2CAP state). */
int  btlink_adopt(btlink *l, unsigned handle, const unsigned char addr[6],
                  const unsigned char link_key[16], unsigned char key_type, const char *name);
int  btlink_drop_handle(btlink *l, unsigned handle, int wait_ms);
/* When set and true for the address being connected, btlink_connect /
 * btlink_accept stop at once (Create Connection Cancel / disconnect). */
extern int (*btlink_abort_connect)(const unsigned char addr[6]);
/* 1 if the pending page command is a Connect/Reconnect of this headset. */
extern int (*btlink_press_is_for)(const unsigned char addr[6]);
/* 1 if addr is one of our saved headsets (set by main). */
extern int (*btlink_saved_peer)(const unsigned char addr[6]);
/* Reject_Connection_Request for addr (reason 0x0D-0x0F); clears tracking. */
void btlink_reject_request(hci_t hci, const unsigned char addr[6], unsigned char reason);
/* Accept an incoming ACL request outside any link (to close it cleanly). */
void btlink_accept_request(hci_t hci, const unsigned char addr[6], int stay_peripheral);
/* HCI Disconnect for a raw handle (one we accepted only to close it). */
void btlink_hci_disconnect(hci_t hci, unsigned handle, unsigned char reason);
/* 1 if this link was accepted from the headset (it called us). */
int  btlink_is_incoming(const btlink *l);
/* Idle: page scan on once (on=1), restored once (on=0). */
void btlink_page_scan_hold(hci_t hci, int on);
/* Called when an ACL comes up (page answered or incoming accepted). */
extern void (*btlink_on_acl_up)(const unsigned char addr[6]);

/* Best-effort HCI Disconnect of known/likely handles before CREATE.
 * Avoids 0x0b ACL-already-exists after a prior spike left a stale link.
 * Never sweeps 0..0xFF; aborts on Disconnect transport/errno fail. */
int  btlink_drop_stale(btlink *l, const unsigned char addr[6]);

/* Pump HCI. Returns -1 if transport dead. */
int btlink_pump(btlink *l, int timeout_ms);

/* Open L2CAP channel to PSM; blocks (pumping) until open or timeout.
 * Returns local SCID (>0) or 0. Always opens a fresh outbound channel. */
unsigned btlink_chan_open(btlink *l, unsigned psm, int timeout_ms);
void     btlink_chan_close(btlink *l, unsigned scid);
/* Close every channel (inbound or outbound) for this PSM. */
void     btlink_chan_close_psm(btlink *l, unsigned psm);
/* Close inbound SDP only — keep AVDTP signaling/media and outbound SDP. */
void     btlink_close_inbound_sdp(btlink *l);
int      btlink_chan_is_open(const btlink *l, unsigned scid);
/* First *outbound* open channel for PSM, or 0. Inbound SDP is not reusable
 * as an SDP client (remote opened it to query us). */
unsigned btlink_chan_find_psm(const btlink *l, unsigned psm);

/* Send on open channel identified by our SCID. */
int btlink_l2_send(btlink *l, unsigned scid, const unsigned char *d, int len);

/* Handler for data on channels the REMOTE opened on psm (e.g. an extra
 * AVDTP channel). fn NULL clears it. */
void btlink_set_inbound_rx(btlink *l, unsigned psm, btlink_rx_fn fn, void *ud);
/* AVRCP absolute volume (0..127). set_volume sends SetAbsoluteVolume and a
 * VOLUME_CHANGED notification when the control channel is open. volume()
 * returns the current value; *changed = 1 once after a headset change.
 * state(): bit3 the headset applies the volume itself (no software scaling),
 * bit0 channel open, bit1 headset uses absolute volume,
 * bit2 volume notifications registered. connect(): open AVRCP ourselves. */
void btlink_avrcp_set_volume(btlink *l, int vol);
int  btlink_avrcp_volume(btlink *l, int *changed);
int  btlink_avrcp_state(const btlink *l);
int  btlink_avrcp_connect(btlink *l);
/* Headset battery as AVRCP status 0..4 (avrcp.h AVRCP_BATT_*), -1 unknown. */
int  btlink_avrcp_battery(const btlink *l);
/* Ask the headset for its volume again (re-register VOLUME_CHANGED; the
 * INTERIM answer carries the level). 0 = not sent (no channel, or the
 * headset refused the registration before). */
int  btlink_avrcp_requery(btlink *l);
void btlink_avrcp_stats(const btlink *l, unsigned long *cmds, unsigned long *rsps,
                        unsigned long *reports, int *refused);
/* Battery percent the headset reported over HFP (0..100), -1 none yet. */
int  btlink_hfp_battery(const btlink *l);
/* Bumped each time the headset itself changed the volume (its buttons/app). */
int  btlink_avrcp_headset_moves(const btlink *l);
/* Last HCI Read RSSI (signed, 127 unknown) and Read Link Quality (0..255,
 * -1 unknown) for this link; polled about once a second while it is up. */
void btlink_link_quality(const btlink *l, int *rssi, int *lq);

void btlink_set_rx(btlink *l, unsigned scid, btlink_rx_fn fn, void *ud);
/* Config-stuck limit for non-media channels (0 = default 8 s). */
void btlink_set_cfg_timeout(btlink *l, long ms);

/* Wait for next data PDU on scid (not signaling). Returns length or 0. */
int btlink_wait_rx(btlink *l, unsigned scid, unsigned char *out, int max,
                   int timeout_ms);

/* MediaTek: 1 once when the headset re-configured this open media channel
 * and that has settled; the stream should SUSPEND/START so it re-binds. */
int btlink_media_rebind_due(btlink *l, unsigned scid);
/* MTU the peer announced for this channel (0 if unknown). */
unsigned btlink_chan_peer_mtu(const btlink *l, unsigned scid);
/* ACL packets queued waiting for controller credits. */
/* Like btlink_l2_send, but the packet is media: while no credit is free it
 * waits in a short queue (btlink_tx_queue_max) and the oldest is dropped
 * whole when the queue is full. */
int      btlink_l2_send_media(btlink *l, unsigned scid, const unsigned char *d, int len);
/* Media packets dropped so far on this link. */
long     btlink_tx_dropped(const btlink *l);
long     btlink_acl_gap_avg(const btlink *l);   /* ms between ACL completions while busy */
/* ms since the controller last returned a credit for our packets while some
 * are outstanding (0 if none outstanding): a link that died quietly. */
long     btlink_ms_since_credit(const btlink *l);
int      btlink_tx_queue_max(void);
/* Per-link media queue depth (auto-tuned by the caller), pacing interval
 * (audio ms per media packet) and raw counters for runtime measurement. */
int      btlink_media_cap(const btlink *l);
void     btlink_set_media_cap(btlink *l, int n);
void     btlink_set_media_pace(btlink *l, long ms_per_packet);
void     btlink_tx_counters(const btlink *l, unsigned long *sent, unsigned long *credits,
                            int *limit);
int      btlink_tx_backlog(const btlink *l);

/* scid of an OPEN channel the remote opened to us on psm, 0 if none. */
unsigned btlink_chan_find_inbound(const btlink *l, unsigned psm);
/* An open inbound channel on psm other than not_scid (A2DP media next to signalling). */
unsigned btlink_chan_find_inbound_other(const btlink *l, unsigned psm, unsigned not_scid);

/* Page scan for up to timeout_ms and accept the first Connection Request
 * from one of the n saved addresses, answering with its stored key.
 * 1 = encrypted link up (*which = index). Scan mode is restored. */
int  btlink_accept(btlink *l, const unsigned char (*addrs)[6],
                   const unsigned char (*keys)[16], const unsigned char *key_types,
                   int n, int timeout_ms, int *which);

#endif
