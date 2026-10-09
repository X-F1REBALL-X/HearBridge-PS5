/* High-level A2DP Source session for HearBridge.
 *
 * Lifecycle (planned): open HCI → find/pair sink → AVDTP+SBC → stream
 * Avcap2 PCM. This header is the public surface; implementations land in
 * stages (see docs/A2DP-PLAN.md). Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_A2DP_H
#define HEARBRIDGE_A2DP_H

#include "hci.h"

#include <stdint.h>

typedef struct a2dp_session a2dp_session;

typedef struct {
    /* Optional: force a BD_ADDR (little-endian). All-zero = scan then pick. */
    unsigned char prefer_addr[6];
    int inquiry_seconds;    /* default 12 for pair spike */
} a2dp_open_opts;

#define A2DP_INQ_MAX 32
#define A2DP_NAME_MAX 64

typedef struct {
    unsigned char addr[6];
    uint32_t cod;
    int rssi;                 /* 0 if unknown (plain Inquiry Result) */
    int have_rssi;
    int audio_likely;         /* Audio/Video major or Audio service class */
    unsigned char psrm;       /* page scan repetition mode from inquiry */
    unsigned clock_offset;    /* 15-bit clock; bit15 set when valid */
    char name[A2DP_NAME_MAX]; /* empty if unknown */
} a2dp_inq_dev;

typedef struct {
    unsigned char addr[6];
    char name[A2DP_NAME_MAX];
    uint32_t cod;
    int handle;               /* ACL handle (12-bit), -1 if none */
    int paired;               /* link key received */
    int encrypted;            /* Encryption Change enabled */
    unsigned char link_key[16];
    unsigned char key_type;
    int need_pair_mode;       /* never answered pairing: not in pairing mode */
} a2dp_pair_result;

/* Opens transport + runs controller setup. Does not pair yet.
 * Returns NULL on failure (reason logged). */
a2dp_session *a2dp_open(hci_t hci, const a2dp_open_opts *opts);

void a2dp_close(a2dp_session *s);

/* 1 if controller answered (BD_ADDR known). */
int a2dp_controller_ok(const a2dp_session *s);

/* Copies local BD_ADDR if known. Returns 0 if unknown. */
int a2dp_local_addr(const a2dp_session *s, unsigned char out[6]);

/* Classic inquiry. Fills out[0..*nfound). Returns 1 if inquiry completed
 * (even with zero devices), 0 on transport/command failure.
 * Optionally Remote-Name-Requests found devices (audio first). */
/* When set and returning nonzero, a running inquiry / name fetch stops
 * early (the page sent a command) and a2dp_inquiry returns what it has. */
extern int (*a2dp_inquiry_abort)(void);
/* When set, called with the results so far whenever a device or a name
 * arrives during a2dp_inquiry (for a live device list). */
extern void (*a2dp_inquiry_progress)(const a2dp_inq_dev *devs, int n);
int a2dp_inquiry(a2dp_session *s, a2dp_inq_dev *out, int max, int *nfound);

/* Orders headphones and speakers into order[] (see hb_dev_rank): prefer_addr first if
 * seen, then headphones/headset, speakers / portable audio, hands-free, headphone-like
 * names, then strongest RSSI. Everything else is left out. Returns the count. */
/* Scan log: forget which devices were already logged (new scan). */
void a2dp_rank_log_reset(void);
int a2dp_rank_sinks(const a2dp_inq_dev *found, int n,
                    const unsigned char prefer_addr[6], int *order, int max);

/* First candidate from a2dp_rank_sinks, or -1. */
int a2dp_pick_sink(const a2dp_inq_dev *found, int n,
                   const unsigned char prefer_addr[6]);

/* Create ACL + SSP auth/encrypt (Secure Simple Pairing, Core Vol 3 Part C). Stores link
 * key under /data/hearbridge/headset.ini on success. Disconnects after.
 * SDP / AVDTP / SBC are NOT done here. Returns 1 on encrypt+key OK. */
int a2dp_pair(a2dp_session *s, const a2dp_inq_dev *target, a2dp_pair_result *out);
/* Set: a successful pair leaves the encrypted ACL up (out->handle) for
 * btlink_adopt; ACL data that arrives meanwhile stays queued. */
extern int a2dp_pair_keep_acl;
/* Nonzero: wall-clock end (now_ms) of a manual scan. Inquiries are shortened
 * or skipped and name lookups stop to finish by then. */
extern long a2dp_scan_deadline_ms;

#endif
