/* Developed by X-F1REBALL-X. See txpath.h. */
#include "txpath.h"
#include <string.h>

int hb_acl_frame_handle(const unsigned char *f, int n)
{
    if (!f || n < 4) return -1;
    return (f[0] | (f[1] << 8)) & 0x0FFF;
}

int hb_evt_disc_handle(const unsigned char *ev, int n)
{
    if (!ev || n < 6 || ev[0] != 0x05 || ev[2] != 0) return -1;
    return (ev[3] | (ev[4] << 8)) & 0x0FFF;
}

int hb_evt_is_nocp(const unsigned char *ev, int n)
{
    int k, m;
    if (!ev || n < 3 || ev[0] != 0x13) return 0;
    m = ev[2];
    for (k = 0; k < m && 3 + k * 4 + 3 < n; k++)
        if (ev[3 + k * 4 + 2] | ev[3 + k * 4 + 3]) return 1;
    return 0;
}

void hb_txpath_init(hb_txpath *t)
{
    memset(t, 0, sizeof *t);
}

int hb_txpath_stall(hb_txpath *t, int spare_open, long now)
{
    if (t->on_spare || t->spare_bad || !spare_open) return 0;
    t->on_spare = 1;
    t->t_switch = now;
    t->sent_spare = t->nocp_spare = 0;
    return 1;
}

void hb_txpath_sent(hb_txpath *t)
{
    if (t->on_spare) t->sent_spare++;
}

void hb_txpath_nocp(hb_txpath *t)
{
    if (t->on_spare) t->nocp_spare++;
}

int hb_txpath_check(hb_txpath *t, long now)
{
    if (!t->on_spare || t->nocp_spare || !t->sent_spare) return 0;
    if (now - t->t_switch < HB_SPARE_PROVE_MS) return 0;
    t->on_spare = 0;
    t->spare_bad = 1;
    return 1;
}

int hb_txpath_disc(hb_txpath *t)
{
    if (!t->on_spare) return 0;
    t->on_spare = 0;
    return 1;
}
