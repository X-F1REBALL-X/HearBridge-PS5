/* Developed by X-F1REBALL-X.
 * Adaptive SBC bitpool: steps down quickly when the radio falls behind
 * (media queue growing or packets dropped), creeps back up after a calm
 * period. Pure logic, no I/O, so the host test can simulate a slow link. */
#ifndef HB_RATE_H
#define HB_RATE_H

#define HB_RATE_FLOOR 22   /* lowest bitpool we step down to (if the range allows) */
#define HB_RATE_CEIL  45   /* highest bitpool we step up to (if the range allows) */

typedef struct {
    int lo, hi;            /* allowed range (configured range ∩ floor/ceiling) */
    int cur;
    long t_down, t_up, t_calm;  /* ms: last decrease, last increase, start of calm */
    long last_drops;
    int downs, ups;
} hb_rate;

/* lo/hi = configured bitpool range, start = first bitpool. */
void hb_rate_init(hb_rate *r, int lo, int hi, int start, long now_ms);

/* Call often (every packet or so). queue = media packets waiting,
 * qmax = queue capacity, drops = total media packets dropped so far.
 * Returns the bitpool to use from now on. */
int hb_rate_update(hb_rate *r, long now_ms, int queue, int qmax, long drops);

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

#endif
