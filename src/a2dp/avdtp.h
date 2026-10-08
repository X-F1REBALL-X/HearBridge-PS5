/* AVDTP signaling + media — A2DP Source toward a headset sink.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_AVDTP_H
#define HEARBRIDGE_AVDTP_H

#include "btlink.h"

#include <stdint.h>

#define AVDTP_PSM 0x0019

/* SBC flavours (codec ids from hsprefs.h):
 *   SBC     joint stereo, bitpool up to 53 (A2DP high quality)
 *   SBC HQ  joint stereo up to the sink's maximum (> 53, e.g. 60)
 *   SBC-XQ  dual channel, each channel its own bitpool, up to 38
 *           (~490 kbit/s at 48 kHz); needs a sink that takes dual channel
 *           and a bitpool of at least 35. */
#define HB_XQ_MIN_BP  35
#define HB_XQ_CEIL    38
#define HB_XQ_LOW_BP  30   /* auto drops XQ when it cannot get above this */
#define HB_HQ_MAX_BP  64
typedef struct {
    uint8_t cfg[4];         /* SBC codec IE to configure */
    int codec;              /* HB_CODEC_SBC / _SBC_HQ / _SBC_XQ */
    int start_bp, ceil;     /* first bitpool, highest the controller may use */
    int avail;              /* bit per codec id the sink can take */
    const char *name;       /* "SBC", "SBC HQ", "SBC-XQ" */
} avdtp_codec_pick;
typedef struct {
    int seid;               /* sink SEID (ACP) */
    uint8_t sbc_caps[4];    /* raw SBC codec info element */
    int have_sbc;
    int bitpool_min, bitpool_max;
    int sample_rate;        /* preferred Hz after negotiation, 0 if none */
    int channels;           /* 1 or 2 */
    int joint_stereo;       /* prefer joint stereo when offered */
    int delay_report;       /* sink lists the Delay Reporting capability */
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
    int unsupported_format;   /* the sink cannot take 48 kHz stereo SBC */
    int want_codec, no_xq;    /* request (HB_CODEC_*), see avdtp_sbc_pick() */
    int held_codec, held_bp;  /* last codec/bitpool that held (0 = none) */
    avdtp_codec_pick codec;   /* what was configured */
    int delay_on;             /* Delay Reporting was configured */
    int sink_delay_x10;       /* last DELAY_REPORT from the sink, 1/10 ms (0 = none yet) */
    long delay_reports;
} avdtp_session;

/* Sampling frequency + channel mode octet (A2DP SBC IE octet 0) chosen
 * from the sink's capability octet 0. Capture is 48 kHz stereo and there
 * is no resampler or downmix, so only 48 kHz with a two-channel mode
 * (joint stereo, stereo, dual channel, in that order) is accepted.
 * Returns the octet (non-zero) and sets *joint; returns 0 and sets *why
 * when the sink cannot take that. Pure (avdtp_media.c, host tested). */
int avdtp_sbc_pick_mode(unsigned char caps0, int *joint, const char **why);

/* want = HB_CODEC_AUTO (always plain SBC) or a codec id (falls back to
 * SBC when the sink cannot take it). caps = the sink's SBC capability IE
 * (bitpool min/max in octets 2-3). Returns 0 (and *why) when the sink takes
 * no 48 kHz two-channel SBC. Pure. */
/* held_codec/held_bp: what actually held last time (0 = none). Used only
 * when it is the same codec we are starting. Auto does not start on XQ. */
int avdtp_sbc_pick(const uint8_t caps[4], int want, int no_xq, avdtp_codec_pick *out,
                   const char **why, int held_codec, int held_bp);

/* Open signaling, Discover → GetAllCaps/GetCaps → SetConfiguration →
 * Open → media channel → Start. Returns 1 on success. */
int avdtp_setup(avdtp_session *s, btlink *link, unsigned avdtp_psm, int want_codec, int no_xq);

/* Send one media packet: RTP + A2DP media hdr (frame count) + SBC frames.
 * n_sbc_frames must match len/frame_bytes and be 1..15 (non-fragmented). */
int avdtp_send_media(avdtp_session *s, const unsigned char *sbc_frames, int len,
                     int samples_in_packet, int n_sbc_frames);

void avdtp_teardown(avdtp_session *s);

/* Codec change without dropping the headset: CLOSE the stream, release
 * the media channel, then SET_CONFIGURATION (plain SBC if the sink refuses
 * the flavour) + OPEN + media channel + START on the same signalling
 * channel and ACL. 1 = streaming with the new codec; 0 = failed (the caller
 * falls back, see hb_cswitch). */
int avdtp_switch_codec(avdtp_session *s, int want_codec, int no_xq);

/* Build RTP + media header + SBC into pkt; advances seq/ts. Returns bytes. */
int  avdtp_build_media(avdtp_session *s, const unsigned char *sbc_frames, int len,
                       int samples_in_packet, int n_sbc_frames,
                       unsigned char *pkt, int pkt_max);
/* Debug dump of the first max_pkts media payloads (see avdtp_media.c). */
int  avdtp_dump_open(avdtp_session *s, const char *path, int max_pkts);
void avdtp_dump_packet(avdtp_session *s, const unsigned char *p, int n);
void avdtp_dump_close(avdtp_session *s);

#endif
