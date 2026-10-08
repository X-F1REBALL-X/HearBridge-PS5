/* Developed by X-F1REBALL-X. */
#include "rejoin.h"

int hb_re_listen_ms(int pages)
{
    static const int ms[HB_RE_PAGES] = { 1200, 1800, 2500 };
    if (pages < 0) pages = 0;
    if (pages >= HB_RE_PAGES) return 0;
    return ms[pages];
}

int hb_re_paging(int pages)
{
    return pages >= 0 && pages < HB_RE_PAGES;
}
