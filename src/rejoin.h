/* After the link drops: listen for the headset to connect in, then one
 * short page, a few times, then only listen. Some headsets (the Xbox one)
 * ignore pages for minutes after the host disconnects. Pure. */
#ifndef HEARBRIDGE_REJOIN_H
#define HEARBRIDGE_REJOIN_H

#define HB_RE_PAGES 3

/* How long to listen before page number `pages` (0 = the first one).
 * 0 means no more pages: sit and accept an incoming connection. */
int hb_re_listen_ms(int pages);
/* 1 while a page is still allowed (pages already done is `pages`). */
int hb_re_paging(int pages);

#endif
