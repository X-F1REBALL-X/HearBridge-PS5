/* After the link drops: a short listen (so we do not page in the same
 * instant the host disconnected — some headsets ignore that), then one
 * page, a few times, then only listen. The listens are a second or two,
 * not half a minute. Pure. */
#ifndef HEARBRIDGE_REJOIN_H
#define HEARBRIDGE_REJOIN_H

#define HB_RE_PAGES 3

/* A clock gap counts as "the console slept" only past this: our own
 * blocking page (5 s) and listen slices must never look like a wake (they
 * did, so the headset was paged for ever and the DualSense dropped). */
#define HB_WAKE_GAP_MS 30000

/* Background pages of the saved headset per idle window (then only listen
 * for it). A wake or a Connect press starts a new window. */
#define HB_AUTO_PAGES 3
/* 1 while another background page is allowed (pages done so far). */
int hb_auto_page_ok(int done);

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

/* Scan window: inquiries (1.28 s units) are chained until the deadline.
 * How many still fit before it (500 ms margin); 0 = the scan is over.
 * The scan loop ends on the same rule, so it never spins re-asking for an
 * inquiry that will not start (hb10.log: every ~130 ms for ~1.2 s). */
int hb_scan_slots_left(long deadline, long now);

/* The headset hung up the fresh pairing link (Xbox Wireless Headset after
 * its SDP Device ID query, 0x13): it calls back on its own, sometimes a few
 * seconds later. Listen (page scan, accept it as the target) this long
 * before paging it: a page in that window keeps the radio busy and its call
 * is missed. */
#define HB_PAIR_CALLBACK_MS 9000

#endif
