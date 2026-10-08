/* Developed by X-F1REBALL-X.
 * Adaptive SBC bitpool: steps down quickly when the radio falls behind
 * (media queue growing or packets dropped), creeps back up after a calm
 * period. Pure logic, no I/O, so the host test can simulate a slow link. */
#ifndef HB_RATE_H
#define HB_RATE_H

#define HB_RATE_FLOOR 22   /* lowest bitpool we step down to (if the range allows) */
/* Highest bitpool we step up to: the A2DP high-quality value for 48 kHz
 * joint stereo (~345 kbit/s). The sink's own advertised maximum (and the
 * range accepted in SET_CONFIGURATION) is the real limit; the controller
 * only climbs while the link returns credits with headroom. */
#define HB_RATE_CEIL  53

/* Packets that normally sit in the media queue on a healthy link and are
 * NOT congestion: the pacer holds up to PACE_TARGET_PKTS (3, acl_pool.h)
 * until their due time, and the stream loop runs up to ~40 ms (one more
 * packet) ahead of the audio clock. Only packets beyond this are late. */
#define HB_RATE_SLACK 4

typedef struct {
    int lo, hi;            /* allowed range (configured range ∩ floor/ceiling) */
    int cur;
    long t_down, t_up, t_calm;  /* ms: last decrease, last increase, start of calm */
    long last_drops;
    int downs, ups;
    int cap;               /* temporary ceiling after congestion at cap+1 (0 = none) */
    long t_cap;            /* when that ceiling was set */
    long t_late;           /* since when packets beyond the slack wait (-1 none) */
} hb_rate;

/* lo/hi = configured bitpool range, start = first bitpool. hi is capped at
 * HB_RATE_CEIL; hb_rate_set_ceiling() lifts that for high-quality modes. */
void hb_rate_init(hb_rate *r, int lo, int hi, int start, long now_ms);
/* Highest bitpool to climb to (still within the configured range). */
void hb_rate_set_ceiling(hb_rate *r, int cfg_hi, int ceil);

/* Call often (every packet or so). queue = media packets waiting,
 * qmax = queue capacity, drops = total media packets dropped so far.
 * Returns the bitpool to use from now on.
 *  - down: a drop (-4); late packets (beyond HB_RATE_SLACK) filling half
 *    the room for 300 ms (-3) or staying for 600 ms (-1); at most one step
 *    per 300 ms. Short bursts that drain by themselves do not count.
 *  - up: after 4 s without a drop, step down or late packets lasting
 *    200 ms, +2 per 2 s while
 *    8 or more below the top, then +1. A bitpool that congested the link
 *    is not tried again for 30 s. */
int hb_rate_update(hb_rate *r, long now_ms, int queue, int qmax, long drops);
/* The link stays slow although the queue looks calm (credits returned
 * without headroom): restart the calm period. */
void hb_rate_not_calm(hb_rate *r, long now_ms);

/* Whole SBC frames per media packet that fit the peer's L2CAP MTU:
 * RTP header 12 + SBC media header 1 + n * frame_len <= mtu, n <= 15
 * (4-bit count), at least 1. Recompute whenever the bitpool changes. */
#define HB_MEDIA_HDR 13
int hb_frames_per_packet(int mtu, int frame_len);

/* Media queue depth (packets waiting for the radio) = worst-case added
 * latency before the oldest packet is dropped. Low latency (default):
 * ~200 ms, for games. Stable: ~1 s (the 1.0.x behaviour), rides out
 * longer radio stalls without dropouts. */
#define HB_QUEUE_LOW_MS     200
#define HB_QUEUE_STABLE_MS 1000
#define HB_QUEUE_MIN_PKTS     4
/* Packets that hold about target_ms of audio at pkt_ms per packet
 * (rounded to nearest), at least HB_QUEUE_MIN_PKTS. */
int hb_media_queue_cap(int pkt_ms, int target_ms);

/* Latency target picked on the page (per headset), ms. */
#define HB_LAT_MIN_MS        60
#define HB_LAT_MAX_MS      1000
#define HB_QUEUE_FLOOR_PKTS   8   /* btlink never queues fewer packets */
int hb_latency_clamp(int ms);
/* Below the default target the packets get shorter, so the queue floor
 * (HB_QUEUE_FLOOR_PKTS) still fits in target_ms. Frames/packet ceiling,
 * 0 = no ceiling (MTU fit). */
int hb_latency_frames_cap(int target_ms, int rate_hz, int samples_per);

/* Rough end-to-end delay, PS5 capture to the headset's speaker. Only an
 * estimate: the console's own audio path before capture is not counted,
 * and without a sink delay report the headset's buffer is a typical value. */
#define HB_CAPTURE_MS      21   /* one 1024-frame capture read at 48 kHz */
#define HB_SINK_TYPICAL_MS 150  /* A2DP sink jitter buffer when it reports nothing */
typedef struct {
    int total_ms, capture_ms, packet_ms, queue_ms, radio_ms, sink_ms;
    int sink_reported;          /* sink_ms came from an AVDTP delay report */
} hb_latency;
/* queue_x10 = average backlog in packets x10; sink_x10 = delay report
 * (1/10 ms, 0 = none). */
void hb_latency_estimate(hb_latency *o, int pkt_ms, int queue_x10, int radio_gap_ms,
                         int sink_x10);

#endif
