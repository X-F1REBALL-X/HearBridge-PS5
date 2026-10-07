/* AVDTP signaling + media — A2DP Source toward a headset sink.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_AVDTP_H
#define HEARBRIDGE_AVDTP_H

#include "btlink.h"

#include <stdint.h>

#define AVDTP_PSM 0x0019

typedef struct {
    int seid;               /* sink SEID (ACP) */
    uint8_t sbc_caps[4];    /* raw SBC codec info element */
    int have_sbc;
    int bitpool_min, bitpool_max;
    int sample_rate;        /* preferred Hz after negotiation, 0 if none */
    int channels;           /* 1 or 2 */
    int joint_stereo;       /* prefer joint stereo when offered */
} avdtp_sink_info;

typedef struct {
    btlink *link;
    unsigned psm;
    unsigned sig_scid;
    unsigned media_scid;
    unsigned char label;
    int int_seid;           /* our INT SEID (source) */
    avdtp_sink_info sink;
    int configured;
    int streaming;
    /* negotiated SBC config (4 bytes) */
    uint8_t sbc_cfg[4];
    int bitpool;            /* starting bitpool */
    int bitpool_lo, bitpool_hi; /* configured range (equal = fixed) */
    uint16_t rtp_seq;
    uint32_t rtp_ts;
    uint32_t rtp_ssrc;
    int in_cmd;             /* our command is waiting for its response */
    int remote_closed;      /* sink sent CLOSE / ABORT while streaming */
    void *dump;             /* FILE* for the media debug dump, or NULL */
    int dump_left;
    int quick;              /* teardown: single short command try */
    int peer_opened;          /* signalling channel opened by the headset */
} avdtp_session;

/* Open signaling, Discover → GetAllCaps/GetCaps → SetConfiguration →
 * Open → media channel → Start. Returns 1 on success. */
int avdtp_setup(avdtp_session *s, btlink *link, unsigned avdtp_psm);

/* Send one media packet: RTP + A2DP media hdr (frame count) + SBC frames.
 * n_sbc_frames must match len/frame_bytes and be 1..15 (non-fragmented). */
int avdtp_send_media(avdtp_session *s, const unsigned char *sbc_frames, int len,
                     int samples_in_packet, int n_sbc_frames);

void avdtp_teardown(avdtp_session *s);

/* Build RTP + media header + SBC into pkt; advances seq/ts. Returns bytes. */
int  avdtp_build_media(avdtp_session *s, const unsigned char *sbc_frames, int len,
                       int samples_in_packet, int n_sbc_frames,
                       unsigned char *pkt, int pkt_max);
/* Debug dump of the first max_pkts media payloads (see avdtp_media.c). */
int  avdtp_dump_open(avdtp_session *s, const char *path, int max_pkts);
void avdtp_dump_packet(avdtp_session *s, const unsigned char *p, int n);
void avdtp_dump_close(avdtp_session *s);

#endif
