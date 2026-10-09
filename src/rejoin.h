/* After the link drops: a short listen (so we do not page in the same
 * instant the host disconnected — some headsets ignore that), then one
 * page, a few times, then only listen. The listens are a second or two,
 * not half a minute. Pure. */
#ifndef HEARBRIDGE_REJOIN_H
#define HEARBRIDGE_REJOIN_H

#define HB_RE_PAGES 3

/* How long to listen before page number `pages` (0 = the first one).
 * 0 means no more pages: sit and accept an incoming connection. */
int hb_re_listen_ms(int pages);
/* 1 while a page is still allowed (pages already done is `pages`). */
int hb_re_paging(int pages);

/* Rest mode / resume. A loop that ticks every second or so sees a gap in
 * its clocks when the console slept (the monotonic clock may stop during
 * sleep while the wall clock does not, so both are checked). 1 = the
 * console was asleep for longer than gap_ms between the two ticks. */
int hb_resume_gap(long mono_prev, long mono_now, long real_prev, long real_now, long gap_ms);
/* The Bluetooth USB device went away (rest mode resets it): wait this long
 * before reopen attempt n (0 = first): quick at first, then every 5 s. */
int hb_reopen_delay_ms(int attempt);
/* After a resume or a reopen: background pages of the last headset for this
 * long (a fresh window, even if the idle one had already run out). */
#define HB_RESUME_PAGE_WINDOW_MS 180000

/* Going into rest mode with a headset playing: stop its stream cleanly
 * first (the headset hears a normal disconnect instead of a link that just
 * dies), then bring it back on wake. going_down = the console asked for
 * rest mode, woke = a resume was seen (clock gap / Bluetooth reopened).
 * A rest request that never ends in sleep (cancelled) counts as a wake
 * after HB_REST_GIVEUP_MS. */
enum { HB_REST_NONE = 0, HB_REST_STOP = 1, HB_REST_RESUME = 2 };
#define HB_REST_GIVEUP_MS 30000
typedef struct {
    int  down;        /* between rest request and wake */
    int  had_stream;  /* a headset was playing when it started */
    long quiet_since; /* down and no request seen since (ms), 0 = not yet */
} hb_rest;
void hb_rest_init(hb_rest *r);
int  hb_rest_step(hb_rest *r, int going_down, int woke, int streaming, long now_ms);

#endif
