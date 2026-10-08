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

#endif
