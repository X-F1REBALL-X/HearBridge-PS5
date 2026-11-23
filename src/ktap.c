/* HearBridge fork — kernel tap capture (PS5 12.40).
 *
 * Replaces the libSceAvcap2 capture (TV / Remote-Play stereo mix) with a
 * read-only tap of the uaudio0 playback frame buffer.  That buffer holds
 * the PCM the system audio server hands to the USB headphone endpoint
 * (PULSE Explore dongle / USB headset), i.e. the post-Tempest binaural
 * render, before the dongle's own radio link.
 *
 * Data-only: kernel_copyout reads, page-table validated, no writes, no
 * MMIO, no locks, no stopping the producer.
 */
#include "ktap.h"
#include "diag.h"
#include "log.h"
#include "util.h"

#include <ps5/kernel.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SAMPLE_RATE   48000
#define CHANNELS      2
#define BLOCK_MAX     0x1000
#define RING_FRAMES   16384          /* 341 ms */

extern const intptr_t KERNEL_ADDRESS_DMAP_BASE;
extern const intptr_t KERNEL_ADDRESS_BUS_DATA_DEVICES;
extern int kernel_proc_getpaddr(int pid, unsigned long va,
                                unsigned long *pa, unsigned long *plen);

struct ktap_session {
    uint64_t sc;
    uint64_t buf;
    uint64_t end;
    size_t   block_bytes;
    uint64_t last_hash;
    long     last_append_ms;
    long     next_append_ms;
    int16_t  ring[RING_FRAMES * CHANNELS];
    size_t   ring_frames;
    int      dead;
    int      nch;
    int      logged;
};

static int in_dmap(uint64_t va)
{
    uint64_t d = (uint64_t)KERNEL_ADDRESS_DMAP_BASE;
    return d && va >= d && va < d + 0x400000000ull;
}

static int in_kdata(uint64_t va)
{
    uint64_t t = (uint64_t)KERNEL_ADDRESS_TEXT_BASE;
    uint64_t d = (uint64_t)KERNEL_ADDRESS_DATA_BASE;
    return t && va >= t && va < d + 0x3000000ull;
}

static int kread(uint64_t va, void *dst, size_t len)
{
    uint8_t *p = dst;

    if (!in_dmap(va) && !in_kdata(va))
        return -1;
    while (len) {
        unsigned long pa, plen;
        size_t n;

        if (!va || kernel_proc_getpaddr(0, va, &pa, &plen) || !plen)
            return -1;
        n = len < plen ? len : (size_t)plen;
        if (kernel_copyout(va, p, n))
            return -1;
        va += n;
        p += n;
        len -= n;
    }
    return 0;
}

static int kstr(uint64_t va, char *out, size_t cap)
{
    size_t i;

    for (i = 0; i + 1 < cap; i++) {
        if (kread(va + i, out + i, 1))
            return -1;
        if (!out[i])
            return 0;
    }
    out[cap - 1] = 0;
    return 0;
}

static uint64_t fnv(const uint8_t *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    size_t i;

    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

/* FreeBSD 11.4 struct device / devclass. */
#define D_DEVLINK   0x18
#define D_DEVCLASS  0x48
#define D_NAMEUNIT  0x58
#define D_SOFTC     0x88
#define C_NAME      0x28

static uint64_t find_uaudio(void)
{
    uint64_t dev = 0, sc = 0;
    int i;

    if (kread(KERNEL_ADDRESS_BUS_DATA_DEVICES, &dev, 8))
        return 0;
    for (i = 0; dev && i < 4096; i++) {
        uint64_t dc = 0, s = 0, nu = 0, dn = 0;
        char name[96] = "?", cls[96] = "?";

        if (kread(dev + D_DEVCLASS, &dc, 8) ||
            kread(dev + D_NAMEUNIT, &nu, 8) ||
            kread(dev + D_SOFTC, &s, 8))
            break;
        if (nu)
            kstr(nu, name, sizeof(name));
        if (dc && !kread(dc + C_NAME, &dn, 8) && dn)
            kstr(dn, cls, sizeof(cls));
        if (!strcmp(cls, "uaudio") || !strcmp(name, "uaudio0"))
            sc = s;
        if (kread(dev + D_DEVLINK, &dev, 8))
            break;
    }
    return sc;
}

int ktap_probe(void)
{
    uint64_t sc = find_uaudio();

    if (!sc) {
        log_line("ktap: uaudio0 not found");
        diag_set("audio session", "FAILED: uaudio0 not found");
        return -1;
    }
    log_line("ktap: uaudio0 softc=%#llx", (unsigned long long)sc);
    diag_set("audio session", "kernel tap uaudio0 (%#llx)",
             (unsigned long long)sc);
    return 0;
}

ktap_session *ktap_open(void)
{
    ktap_session *s;
    uint64_t sc, buf = 0, end = 0;

    sc = find_uaudio();
    if (!sc) {
        log_line("ktap: uaudio0 not found");
        diag_set("audio session", "FAILED: uaudio0 not found");
        return NULL;
    }
    /* uaudio_chan[1] (playback) at softc+0x120; buf/end at +0x80/+0x90. */
    if (kread(sc + 0x1A0, &buf, 8) || kread(sc + 0x1B0, &end, 8)) {
        log_line("ktap: buffer pointer read failed");
        diag_set("audio session", "FAILED: pointer read");
        return NULL;
    }
    if (!in_dmap(buf) || end <= buf || end - buf > BLOCK_MAX) {
        log_line("ktap: bad buffer buf=%#llx end=%#llx",
                 (unsigned long long)buf, (unsigned long long)end);
        diag_set("audio session", "FAILED: bad buffer");
        return NULL;
    }
    s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->sc = sc;
    s->buf = buf;
    s->end = end;
    s->block_bytes = (size_t)(end - buf);
    s->next_append_ms = now_ms();
    s->nch = CHANNELS;
    log_line("ktap: reachless open sc=%#llx buf=%#llx len=%zu",
             (unsigned long long)sc, (unsigned long long)buf, s->block_bytes);
    diag_set("audio session", "ok (kernel tap %zu B blocks)", s->block_bytes);
    return s;
}

void ktap_close(ktap_session *s)
{
    if (!s)
        return;
    log_line("ktap: close");
    free(s);
}

int ktap_read_s16(ktap_session *s, int16_t *out, int max_frames,
                            float *peak_abs)
{
    uint8_t a[BLOCK_MAX], b[BLOCK_MAX];
    uint64_t deadline;
    size_t n, i;

    if (peak_abs)
        *peak_abs = 0.f;
    if (!s || !out || max_frames <= 0 || s->dead)
        return -1;

    deadline = (uint64_t)now_ms() + 50;
    while (s->ring_frames < (size_t)max_frames &&
           (uint64_t)now_ms() < deadline) {
        uint64_t h, t;

        if (kread(s->buf, a, s->block_bytes) ||
            kread(s->buf, b, s->block_bytes)) {
            s->dead = 1;
            return -1;
        }
        if (memcmp(a, b, s->block_bytes) != 0) {
            usleep(500);                 /* producer writing; retry */
            continue;
        }
        h = fnv(a, s->block_bytes);
        t = (uint64_t)now_ms();
        /* 768 B = 4 ms.  Append one block per 4 ms, not on every 1 ms
         * change, or the SBC encoder runs at 2-4x real time and congests
         * the link (crackle / dropouts). */
        if (t >= (uint64_t)s->next_append_ms) {
            size_t frames = s->block_bytes / (CHANNELS * 2);

            if (s->ring_frames + frames > RING_FRAMES) {
                size_t drop = s->ring_frames + frames - RING_FRAMES;
                memmove(s->ring, (uint8_t *)s->ring + drop * CHANNELS * 2,
                        (s->ring_frames - drop) * CHANNELS * 2);
                s->ring_frames -= drop;
            }
            memcpy((uint8_t *)s->ring + s->ring_frames * CHANNELS * 2,
                   a, s->block_bytes);
            s->ring_frames += frames;
            s->last_hash = h;
            s->last_append_ms = (long)t;
            s->next_append_ms += 4;
            if ((uint64_t)s->next_append_ms < t)
                s->next_append_ms = (long)t + 4;
        }
        if (s->ring_frames < (size_t)max_frames)
            usleep(1000);
    }

    n = s->ring_frames < (size_t)max_frames ? s->ring_frames
                                            : (size_t)max_frames;
    if (!n)
        return 0;
    memcpy(out, s->ring, n * CHANNELS * 2);
    memmove(s->ring, (uint8_t *)s->ring + n * CHANNELS * 2,
            (s->ring_frames - n) * CHANNELS * 2);
    s->ring_frames -= n;

    if (peak_abs) {
        float pL = 0.f, pR = 0.f;
        for (i = 0; i < n; i++) {
            float l = (float)out[i * 2] / 32768.f;
            float r = (float)out[i * 2 + 1] / 32768.f;
            if (l < 0) l = -l;
            if (r < 0) r = -r;
            if (l > pL) pL = l;
            if (r > pR) pR = r;
        }
        *peak_abs = pL > pR ? pL : pR;
        if ((++s->logged % 200) == 0)
            log_line("ktap chan: L=%.3f R=%.3f n=%d", pL, pR, (int)n);
    }
    return (int)n;
}
