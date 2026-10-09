/* Link quality for the page: one 0..100 score from what the controller
 * reports (HCI Read RSSI / Read Link Quality) and what the stream sees
 * (media packets dropped, queue backlog). Pure. Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_LINKQ_H
#define HEARBRIDGE_LINKQ_H

#define HB_RSSI_UNKNOWN 127

typedef struct {
    long last_drops;      /* dropped counter at the last sample */
    long t_last;          /* ms of the last sample, 0 = none */
    int drops_x10;        /* smoothed drops per minute x10 */
} hb_linkq;

void hb_linkq_init(hb_linkq *q);
/* Feed the dropped-packet counter once a second or so; returns the
 * smoothed drops per minute. */
int  hb_linkq_drops_per_min(hb_linkq *q, long dropped, long now_ms);
/* RSSI part 0..100, -1 unknown. BR/EDR controllers report the distance to
 * the golden receive range (0 = fine, negative = weak); some report plain
 * dBm instead (values under -20), both are handled. */
int  hb_linkq_rssi_score(int rssi);
/* 0..100 from RSSI, link quality (0..255, -1 unknown), drops per minute and
 * queue backlog (packets) against its cap. -1 when nothing is known yet. */
int  hb_linkq_score(int rssi, int lq, int drops_per_min, int backlog, int cap);
/* 0..4 bars for a score (-1 = 0 bars). */
int  hb_linkq_bars(int score);

#endif
