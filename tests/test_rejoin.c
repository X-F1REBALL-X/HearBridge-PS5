/* Developed by X-F1REBALL-X. After a drop: listen, a few gentle pages, then sit. */
#include "rejoin.h"

#include <stdio.h>

static int fails;
#define CHECK(c, m) do { if (c) printf("ok   %s\n", m); else { printf("FAIL %s\n", m); fails++; } } while (0)

int main(void)
{
    int i, prev = 0;
    CHECK(hb_re_paging(0) && hb_re_listen_ms(0) >= 1000 && hb_re_listen_ms(0) <= 2000,
          "after a drop: about a second of listen, then a page");
    CHECK(hb_re_paging(HB_RE_PAGES - 1) && !hb_re_paging(HB_RE_PAGES),
          "only a few pages, then no more");
    CHECK(hb_re_listen_ms(HB_RE_PAGES) == 0 && hb_re_listen_ms(99) == 0,
          "after the pages: sit and accept an incoming connection");
    for (i = 0; i < HB_RE_PAGES; i++) {
        int ms = hb_re_listen_ms(i);
        CHECK(ms > prev && ms <= 3000, i ? "later listens stay short" : "first listen is the short one");
        prev = ms;
    }
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails != 0;
}
