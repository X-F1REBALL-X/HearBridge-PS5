/* Developed by X-F1REBALL-X. */
#include "ctl.h"

#include <string.h>
#include <time.h>

static long mono_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec;
}

long ctl_uptime_s(const hb_ctl *c) { return mono_s() - c->t0_s; }

hb_ctl g_ctl;

void ctl_init(hb_ctl *c, const char *version)
{
    memset(c, 0, sizeof *c);
    pthread_mutex_init(&c->mu, NULL);
    c->gain_pct = HB_GAIN_DEFAULT_PCT;
    c->req_hs_volume = -1;
    c->hs_volume = -1;
    strncpy(c->version, version, sizeof c->version - 1);
    strcpy(c->state, "starting");
    c->t0_s = mono_s();
}

/* Headset volume 0..127 scales the base gain linearly (127 = full base
 * gain), so the headset's own volume buttons / app move our level too. */
int ctl_effective_gain_milli(int gain_pct, int muted, int hs_volume)
{
    long g;
    if (muted) return 0;
    if (gain_pct < 0) gain_pct = 0;
    if (gain_pct > HB_GAIN_MAX_PCT) gain_pct = HB_GAIN_MAX_PCT;
    g = (long)gain_pct * 10;                       /* x1000 */
    if (hs_volume >= 0) g = g * (hs_volume > 127 ? 127 : hs_volume) / 127;
    return (int)g;
}

void ctl_clear_link(hb_ctl *c, int drop_device)
{
    if (drop_device) c->device[0] = 0;
    c->hs_volume = -1;
    c->avrcp = 0;
    c->pkts = c->frames = c->empty_reads = c->dropped = 0;
    c->peak_milli = c->out_peak_milli = 0;
    c->sample_rate = c->bitpool = c->backlog = 0;
    c->per_packet = c->bitpool_lo = c->bitpool_hi = 0;
}
