/* Developed by X-F1REBALL-X. */
#include "ctl.h"
#include "rate.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long mono_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec;
}

long ctl_uptime_s(const hb_ctl *c) { return mono_s() - c->t0_s; }

hb_ctl g_ctl;

static uint64_t mix64(uint64_t x)
{
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

void ctl_new_token(hb_ctl *c)
{
    static uint64_t counter;
    unsigned char b[HB_TOKEN_LEN / 2];
    size_t got = 0;
    int i;
    FILE *f = fopen("/dev/urandom", "rb");

    if (f) {
        got = fread(b, 1, sizeof b, f);
        fclose(f);
    }
    if (got != sizeof b) {
        /* No /dev/urandom: not cryptographic, but unguessable enough for a
         * LAN page (clock nanoseconds, pid, stack address, call count). */
        struct timespec rt, mt;
        uint64_t s;
        clock_gettime(CLOCK_REALTIME, &rt);
        clock_gettime(CLOCK_MONOTONIC, &mt);
        s = (uint64_t)rt.tv_sec * 1000000007ULL ^ (uint64_t)rt.tv_nsec ^
            ((uint64_t)mt.tv_nsec << 21) ^ ((uint64_t)getpid() << 40) ^
            (uint64_t)(uintptr_t)&s ^ ++counter;
        for (i = 0; i < (int)sizeof b; i++) {
            if (!(i % 8)) s = mix64(s + 0x9e3779b97f4a7c15ULL);
            b[i] = (unsigned char)(s >> (8 * (i % 8)));
        }
    }
    for (i = 0; i < (int)sizeof b; i++)
        snprintf(c->token + 2 * i, 3, "%02x", b[i]);
}

void ctl_init(hb_ctl *c, const char *version)
{
    memset(c, 0, sizeof *c);
    pthread_mutex_init(&c->mu, NULL);
    c->gain_pct = HB_GAIN_DEFAULT_PCT;
    c->req_hs_volume = -1;
    c->hs_volume = -1;
    c->latency_ms = HB_QUEUE_LOW_MS;
    strncpy(c->version, version, sizeof c->version - 1);
    strcpy(c->state, "starting");
    c->t0_s = mono_s();
    ctl_new_token(c);
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
    c->codec[0] = 0;
    c->codec_avail = 0;
    c->xq_low = 0;
    c->sample_rate = c->bitpool = c->backlog = 0;
    c->per_packet = c->bitpool_lo = c->bitpool_hi = 0;
}

void ctl_event_locked(hb_ctl *c, const char *line)
{
    if (!c || !line || !line[0]) return;
    if (c->event_n >= HB_EVENT_N) {
        memmove(c->events[0], c->events[1], sizeof c->events[0] * (HB_EVENT_N - 1));
        c->event_n = HB_EVENT_N - 1;
    }
    snprintf(c->events[c->event_n], sizeof c->events[0], "%s", line);
    c->event_n++;
}

void ctl_event(hb_ctl *c, const char *line)
{
    if (!c || !line || !line[0]) return;
    CTL_LOCK(c);
    ctl_event_locked(c, line);
    CTL_UNLOCK(c);
}
