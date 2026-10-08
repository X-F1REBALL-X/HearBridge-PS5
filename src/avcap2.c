/* HearBridge — system audio capture through libSceAvcap2 (48 kHz stereo).
 *
 * NIDs + OpenAudio ABI: 16-byte meta, buflen=0x2000, no authid elevation
 * by default, restart on the sticky 0x81950001 status.
 */
#include "avcap2.h"
#include "creds.h"
#include "diag.h"
#include "log.h"
#include "util.h"

#include <ps5/kernel.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NID_LoadStartModule "wzvqT4UqKX8"

#define NID_Avcap2Initialize "svzPXluOz8U"
#define NID_Avcap2Terminate  "gojqghDU+1Y"
#define NID_Avcap2OpenAudio  "3nHpE7Dp5SE"
#define NID_Avcap2Close      "tR7gPe1i8hw"
#define NID_Avcap2Start      "8CGNCwBsItI"
#define NID_Avcap2Stop       "Z5dKK0xnQ8g"
#define NID_Avcap2ReadAudio  "WhsHggqdwcg"

/* Avcap2 facility 0x8195 — bit-like status codes. */
#define AVCAP2_INVALID 0x81950001u /* sticky in 0.0.5; log and return -1 */
#define AVCAP2_EMPTY   0x81950002u
#define AVCAP2_OVERRUN 0x81950004u
#define AVCAP2_ENDED   0x81950008u

#define OPEN_PARAM_SIZE 0x38
#define META_SIZE       0x10
#define SAMPLE_RATE     48000
#define CHANNELS        2
/* capture_read(buf, 0x2000) → ReadAudio buflen=8192. */
#define READ_BYTES      0x2000

/* No authid elevation needed for Avcap2 capture. We try default first. */
#define AUTHID_AVCAP2   0x4900000000000002ULL
#define AUTHID_FALLBACK 0x4801000000000013ULL

typedef int (*fn_LoadStart)(const char *, size_t, const void *, uint32_t, void *, int *);
typedef int (*fn_Init)(void);
typedef int (*fn_Term)(void);
typedef int (*fn_Open)(int64_t *outhandle, void *param, int flags);
typedef int (*fn_Close)(int64_t handle);
typedef int (*fn_Start)(int64_t handle);
typedef int (*fn_Stop)(int64_t handle);
typedef int (*fn_Read)(int64_t handle, void *buf, size_t buflen, void *out_meta, int flag);

static int resolve_libkernel(uint32_t *out)
{
    static const char *names[] = {
        "libkernel_web.sprx", "libkernel.sprx", "libkernel_sys.sprx", NULL
    };
    int i;

    for (i = 0; names[i]; i++) {
        if (kernel_dynlib_handle(-1, names[i], out) == 0 && *out)
            return 0;
    }
    return -1;
}

static int load_sprx(fn_LoadStart LoadStart, const char *basename)
{
    static const char *roots[] = {
        "/system/common/lib/",
        "",
        "/system/priv/lib/",
        "/system_ex/common_ex/lib/",
        "/system_ex/priv_ex/lib/",
        NULL
    };
    uint32_t handle = 0;
    char path[256];
    int i, res, rv;

    if (kernel_dynlib_handle(-1, basename, &handle) == 0 && handle) {
        log_line("avcap2: %s already loaded (handle %#x)", basename, handle);
        diag_set(basename, "already loaded (handle %#x)", handle);
        return 0;
    }
    if (!LoadStart) {
        log_line("avcap2: no sceKernelLoadStartModule");
        diag_set(basename, "FAILED: sceKernelLoadStartModule not resolved");
        return -1;
    }
    for (i = 0; roots[i]; i++) {
        if (roots[i][0])
            snprintf(path, sizeof path, "%s%s", roots[i], basename);
        else
            snprintf(path, sizeof path, "%s", basename);
        res = -1;
        rv = LoadStart(path, 0, 0, 0, 0, &res);
        log_line("avcap2: LoadStart %s -> rv=%d res=%d", path, rv, res);
        if (kernel_dynlib_handle(-1, basename, &handle) == 0 && handle) {
            log_line("avcap2: %s handle %#x", basename, handle);
            diag_set(basename, "loaded from %s (rv %d, res %d, handle %#x)", path, rv, res, handle);
            return 0;
        }
    }
    log_line("avcap2: failed to load %s", basename);
    diag_set(basename, "FAILED to load (last try %s: rv %d res %d)", path, rv, res);
    return -1;
}

/* Same switch as before 1.0.1-fw13.60 (no restore): see creds.c. */
static void elevate(uint64_t authid)
{
    (void)creds_elevate(authid, NULL, "avcap2");
}

static void fill_open_param(unsigned char *p)
{
    memset(p, 0, OPEN_PARAM_SIZE);
    *(uint64_t *)(p + 0x00) = OPEN_PARAM_SIZE; /* 0x38 */
    *(uint64_t *)(p + 0x08) = 0x18;
    *(uint32_t *)(p + 0x14) = 2;               /* source = Remote Play mix */
    *(uint32_t *)(p + 0x1c) = 0xffffffffu;
}

/* OpenAudio param layout (16-byte meta). First u32 OUT is
 * typically 0x10 (size). Pre-set size in case the library treats it as IN/OUT. */
static void fill_meta(unsigned char *m)
{
    memset(m, 0, META_SIZE);
    *(uint32_t *)(m + 0x00) = META_SIZE;
}

static void hexdump_n(const char *tag, const unsigned char *p, int n)
{
    char line[128];
    int i, off = 0;

    while (off < n) {
        int pos = 0;
        for (i = 0; i < 16 && off + i < n; i++)
            pos += snprintf(line + pos, sizeof line - (size_t)pos, "%02x%s",
                            p[off + i], (i + 1) % 16 ? " " : "");
        line[pos] = '\0';
        log_line("avcap2: %s +%#x %s", tag, off, line);
        off += 16;
    }
}

/* Open+Start with OpenAudio param. auth_mode: 0=leave authid, 1=Avcap2, 2=fallback. */
static int open_and_start(fn_Init Init, fn_Term Term, fn_Open Open, fn_Close Close,
                          fn_Start Start, int64_t *outhandle, int auth_mode)
{
    unsigned char open_param[OPEN_PARAM_SIZE];
    int64_t handle = 0;
    int rv;

    if (auth_mode == 1)
        elevate(AUTHID_AVCAP2);
    else if (auth_mode == 2)
        elevate(AUTHID_FALLBACK);
    else
        log_line("avcap2: leaving authid %#llx (no elevate)",
                 (unsigned long long)kernel_get_ucred_authid(-1));

    log_line("avcap2: before Initialize (auth_mode=%d)", auth_mode);
    rv = Init();
    log_line("avcap2: Initialize -> %#x", (unsigned)rv);
    if (rv != 0) {
        if (Term) {
            log_line("avcap2: before Terminate (Init failed, clear)");
            log_line("avcap2: Terminate -> %#x", (unsigned)Term());
        }
        log_line("avcap2: before Initialize (retry)");
        rv = Init();
        log_line("avcap2: Initialize (retry) -> %#x", (unsigned)rv);
        if (rv != 0) {
            diag_set("audio open", "auth_mode %d: sceAvcap2Initialize failed %#x", auth_mode, (unsigned)rv);
            return rv;
        }
    }

    fill_open_param(open_param);
    log_line("avcap2: OpenAudio param layout (3-arg, handle*+param+0)");
    hexdump_n("param", open_param, OPEN_PARAM_SIZE);

    log_line("avcap2: before Open(&handle, param, 0)");
    rv = Open(&handle, open_param, 0);
    log_line("avcap2: Open -> rv=%#x handle=%#llx",
             (unsigned)rv, (unsigned long long)handle);
    diag_set("audio open", "auth_mode %d: Initialize ok, OpenAudio rv %#x handle %#llx",
             auth_mode, (unsigned)rv, (unsigned long long)handle);
    if (rv != 0 || handle == 0) {
        if (Term) {
            log_line("avcap2: before Terminate (Open failed)");
            log_line("avcap2: Terminate -> %#x", (unsigned)Term());
        }
        return rv != 0 ? rv : -1;
    }

    log_line("avcap2: before Start(handle=%#llx)", (unsigned long long)handle);
    rv = Start(handle);
    log_line("avcap2: Start -> %#x", (unsigned)rv);
    if (rv != 0) {
        diag_set("audio open", "auth_mode %d: Start failed %#x", auth_mode, (unsigned)rv);
        Close(handle);
        if (Term) {
            log_line("avcap2: before Terminate (Start failed)");
            log_line("avcap2: Terminate -> %#x", (unsigned)Term());
        }
        return rv;
    }

    *outhandle = handle;
    return 0;
}

/* capture_restart: Stop/Close/Term then start_capture again. */
static int session_restart(fn_Init Init, fn_Term Term, fn_Open Open, fn_Close Close,
                           fn_Start Start, fn_Stop Stop, int64_t *handle, int auth_mode)
{
    log_line("avcap2: session_restart (handle=%#llx auth_mode=%d)",
             (unsigned long long)*handle, auth_mode);
    if (*handle) {
        Stop(*handle);
        Close(*handle);
        *handle = 0;
    }
    if (Term) {
        log_line("avcap2: Terminate (restart) -> %#x", (unsigned)Term());
    }
    return open_and_start(Init, Term, Open, Close, Start, handle, auth_mode);
}

/* ---- streaming session (A2DP path) ---------------------------------------- */

struct avcap2_session {
    fn_Init Init;
    fn_Term Term;
    fn_Open Open;
    fn_Close Close;
    fn_Start Start;
    fn_Stop Stop;
    fn_Read Read;
    int64_t handle;
    int auth_mode;
    float *buf;
    int restarts;
    long last_ok_ms;
    long last_restart_ms;
    int read_flag;
    int dead;
    uint64_t reads_ok;
    uint64_t reads_empty;
    uint64_t reads_invalid;
    uint64_t reads_other;
    int logged_first;
    /* Detected layout of the capture buffer. Defaults: float32, 2 ch. */
    int fmt_s16;          /* 1 = int16 samples, 0 = float32 */
    int nch;              /* interleaved channels in the buffer */
    int fmt_checked;
    long rate_t0;         /* channel-count check: bytes over ~2 s */
    uint64_t rate_bytes;
    int rate_done;
    uint64_t nonfinite;
};

int avcap2_probe(void)
{
    static const char *const nids[] = {
        NID_Avcap2Initialize, NID_Avcap2OpenAudio, NID_Avcap2Start,
        NID_Avcap2ReadAudio, NID_Avcap2Stop, NID_Avcap2Close,
    };
    uint32_t kh = 0, ah = 0;
    fn_LoadStart LoadStart;
    int i, missing = 0;

    if (resolve_libkernel(&kh) != 0) {
        diag_set("audio libs", "FAILED: libkernel handle not found");
        return -1;
    }
    LoadStart = (fn_LoadStart)kernel_dynlib_resolve(-1, kh, NID_LoadStartModule);
    if (load_sprx(LoadStart, "libSceIpmi.sprx") != 0 ||
        load_sprx(LoadStart, "libSceAvcap2.sprx") != 0) {
        diag_set("audio libs", "FAILED: see the libSce*.sprx lines");
        return -1;
    }
    if (kernel_dynlib_handle(-1, "libSceAvcap2.sprx", &ah) != 0 || !ah) {
        diag_set("audio libs", "FAILED: no libSceAvcap2 handle");
        return -1;
    }
    for (i = 0; i < (int)(sizeof nids / sizeof nids[0]); i++)
        if (!kernel_dynlib_resolve(-1, ah, nids[i])) missing++;
    if (missing) diag_set("audio libs", "loaded, %d Avcap2 function(s) missing", missing);
    else diag_set("audio libs", "loaded, all Avcap2 functions resolved");
    log_line("avcap2: probe: %d of %d functions missing", missing,
             (int)(sizeof nids / sizeof nids[0]));
    return missing ? -1 : 0;
}

avcap2_session *avcap2_session_open(void)
{
    avcap2_session *s;
    uint32_t kh = 0, ah = 0;
    fn_LoadStart LoadStart = NULL;
    int mode, opened = 0;

    s = calloc(1, sizeof *s);
    if (!s) return NULL;

    if (resolve_libkernel(&kh) != 0) {
        log_line("avcap2: libkernel not found");
        free(s);
        return NULL;
    }
    LoadStart = (fn_LoadStart)kernel_dynlib_resolve(-1, kh, NID_LoadStartModule);
    if (load_sprx(LoadStart, "libSceIpmi.sprx") != 0 ||
        load_sprx(LoadStart, "libSceAvcap2.sprx") != 0) {
        free(s);
        return NULL;
    }
    if (kernel_dynlib_handle(-1, "libSceAvcap2.sprx", &ah) != 0 || !ah) {
        free(s);
        return NULL;
    }
    s->Init  = (fn_Init)kernel_dynlib_resolve(-1, ah, NID_Avcap2Initialize);
    s->Term  = (fn_Term)kernel_dynlib_resolve(-1, ah, NID_Avcap2Terminate);
    s->Open  = (fn_Open)kernel_dynlib_resolve(-1, ah, NID_Avcap2OpenAudio);
    s->Close = (fn_Close)kernel_dynlib_resolve(-1, ah, NID_Avcap2Close);
    s->Start = (fn_Start)kernel_dynlib_resolve(-1, ah, NID_Avcap2Start);
    s->Stop  = (fn_Stop)kernel_dynlib_resolve(-1, ah, NID_Avcap2Stop);
    s->Read  = (fn_Read)kernel_dynlib_resolve(-1, ah, NID_Avcap2ReadAudio);
    if (!s->Init || !s->Open || !s->Start || !s->Read || !s->Stop || !s->Close) {
        log_line("avcap2: missing NIDs");
        diag_set("audio session", "FAILED: Avcap2 functions missing");
        (void)diag_save();
        free(s);
        return NULL;
    }
    s->buf = (float *)malloc((size_t)READ_BYTES);
    if (!s->buf) { free(s); return NULL; }

    for (mode = 0; mode <= 2; mode++) {
        if (open_and_start(s->Init, s->Term, s->Open, s->Close, s->Start,
                           &s->handle, mode) == 0 && s->handle) {
            s->auth_mode = mode;
            opened = 1;
            break;
        }
        s->handle = 0;
    }
    if (!opened) {
        log_line("avcap2: session Open/Start failed");
        diag_set("audio session", "FAILED: Open/Start failed in every auth mode");
        (void)diag_save();
        free(s->buf);
        free(s);
        return NULL;
    }
    s->read_flag = 1;
    s->nch = CHANNELS;
    s->last_ok_ms = now_ms();
    s->last_restart_ms = s->last_ok_ms;
    usleep(100000);
    log_line("avcap2: session open for streaming");
    diag_set("audio session", "ok (auth_mode %d)", s->auth_mode);
    (void)diag_save();
    return s;
}

void avcap2_session_close(avcap2_session *s)
{
    if (!s) return;
    if (s->handle) {
        s->Stop(s->handle);
        s->Close(s->handle);
        s->handle = 0;
    }
    if (s->Term) s->Term();
    free(s->buf);
    free(s);
}

int avcap2_session_read_s16(avcap2_session *s, int16_t *out, int max_frames,
                            float *peak_abs)
{
    unsigned char meta[META_SIZE];
    int got, frames;
    long t_before, now;
    float peak = 0.f;
    extern long now_ms(void);

    if (peak_abs) *peak_abs = 0.f;
    if (!s || !out || max_frames <= 0 || s->dead) return -1;
    fill_meta(meta);
    if (!s->logged_first) {
        log_line("avcap2: before first stream ReadAudio (buflen=%#x meta=%#x flag=%d)",
                 READ_BYTES, META_SIZE, s->read_flag);
        s->logged_first = 1;
    }
    t_before = now_ms();
    got = s->Read(s->handle, s->buf, (size_t)READ_BYTES, meta, s->read_flag);
    now = now_ms();
    if (now - t_before > 200)
        log_line("avcap2: ReadAudio blocked %ld ms (rv=%#x)", now - t_before,
                 (unsigned)got);

    if (got > 0) {
        if (got > READ_BYTES) got = READ_BYTES;
        if (!s->fmt_checked) {
            /* Float32 audio in [-1,1] has almost every word either 0 or with
             * an exponent near 0x3F/0x3E/0x3C... Int16 pairs read as float
             * give huge / NaN / denormal words. Count implausible words. */
            const uint32_t *w = (const uint32_t *)(void *)s->buf;
            int nw = got / 4, bad = 0, nz = 0, k;
            for (k = 0; k < nw; k++) {
                uint32_t e = (w[k] >> 23) & 0xFF;
                if (!w[k] || w[k] == 0x80000000u) continue;
                nz++;
                if (e == 0xFF || e > 0x7F || e < 0x50) bad++;
            }
            {
                const unsigned char *b8 = (const unsigned char *)s->buf;
                log_line("avcap2: first data %02x %02x %02x %02x %02x %02x %02x %02x "
                         "%02x %02x %02x %02x %02x %02x %02x %02x (bytes=%d)",
                         b8[0], b8[1], b8[2], b8[3], b8[4], b8[5], b8[6], b8[7],
                         b8[8], b8[9], b8[10], b8[11], b8[12], b8[13], b8[14],
                         b8[15], got);
            }
            if (nz >= 64) {
                s->fmt_checked = 1;
                s->fmt_s16 = (bad * 4 > nz);   /* >25% implausible floats */
                log_line("avcap2: format check: %d/%d non-zero words implausible "
                         "as float -> %s", bad, nz, s->fmt_s16 ? "int16" : "float32");
            }
        }
        if (!s->rate_done) {
            /* Bytes per second tells the channel count: 48 kHz x 2 ch x 4 B
             * = 384000 B/s; 8 ch float would be ~1.5 MB/s. */
            if (!s->rate_t0) s->rate_t0 = now;
            s->rate_bytes += (uint64_t)got;
            if (now - s->rate_t0 >= 2000) {
                double bps = (double)s->rate_bytes * 1000.0 / (double)(now - s->rate_t0);
                double per = (double)SAMPLE_RATE * (s->fmt_s16 ? 2.0 : 4.0);
                double r = bps / per;
                int ch = (int)(r + 0.5);
                double dev = r - (double)ch;
                /* Only trust a clean multiple (a backlog burst is not). */
                int newch = ((ch == 4 || ch == 6 || ch == 8) &&
                             dev < 0.15 && dev > -0.15) ? ch : 2;
                s->rate_done = 1;
                log_line("avcap2: capture rate %.0f B/s = %.2f ch of %s at %d Hz -> "
                         "using %d ch (L/R = first two)", bps, bps / per,
                         s->fmt_s16 ? "int16" : "float32", SAMPLE_RATE, newch);
                s->nch = newch;
            }
        }
        {
            int bpsamp = s->fmt_s16 ? 2 : 4, nch = s->nch > 0 ? s->nch : 2, f;
            const int16_t *si = (const int16_t *)(void *)s->buf;
            frames = got / (nch * bpsamp);
            if (frames > max_frames) frames = max_frames;
            for (f = 0; f < frames; f++) {
                int c;
                for (c = 0; c < 2; c++) {
                    float v, a;
                    int sample;
                    if (s->fmt_s16) v = (float)si[f * nch + c] / 32768.f;
                    else v = s->buf[f * nch + c];
                    if (!(v == v) || v > 1e30f || v < -1e30f) { /* NaN / inf */
                        s->nonfinite++;
                        v = 0.f;
                    }
                    if (v > 1.f) v = 1.f;
                    if (v < -1.f) v = -1.f;
                    a = v < 0 ? -v : v;
                    if (a > peak) peak = a;
                    sample = (int)(v * 32767.f);
                    out[f * 2 + c] = (int16_t)sample;
                }
            }
        }
        s->last_ok_ms = now;
        s->reads_ok++;
        if (s->reads_ok <= 3 || (s->reads_ok % 50) == 0)
            log_line("avcap2: Read ok #%llu frames=%d peak=%.4f bytes=%d nonfinite=%llu",
                     (unsigned long long)s->reads_ok, frames, (double)peak, got,
                     (unsigned long long)s->nonfinite);
        if (peak_abs) *peak_abs = peak;
        return frames;
    }
    if ((unsigned)got == AVCAP2_EMPTY || (unsigned)got == AVCAP2_OVERRUN) {
        s->reads_empty++;
        if (s->reads_empty <= 3 || (s->reads_empty % 200) == 0)
            log_line("avcap2: Read empty/over #%llu rv=%#x",
                     (unsigned long long)s->reads_empty, (unsigned)got);
        return 0;
    }
    if ((unsigned)got == AVCAP2_ENDED) {
        log_line("avcap2: Read ENDED %#x", (unsigned)got);
        s->dead = 1;
        return -1;
    }
    if ((unsigned)got == AVCAP2_INVALID) {
        s->reads_invalid++;
        if (s->reads_invalid <= 8)
            log_line("avcap2: Read INVALID #%llu %#x",
                     (unsigned long long)s->reads_invalid, (unsigned)got);
        if (now - s->last_ok_ms > 800 && now - s->last_restart_ms > 800 &&
            s->restarts < 3) {
            s->restarts++;
            s->last_restart_ms = now;
            if (s->restarts == 2) s->read_flag = 0;
            log_line("avcap2: stream restart #%d", s->restarts);
            if (session_restart(s->Init, s->Term, s->Open, s->Close, s->Start,
                                s->Stop, &s->handle, s->auth_mode) != 0) {
                s->dead = 1;
                return -1;
            }
            usleep(100000);
            s->last_ok_ms = now_ms();
        }
        return 0;
    }
    s->reads_other++;
    if (s->reads_other <= 8)
        log_line("avcap2: Read other #%llu %#x",
                 (unsigned long long)s->reads_other, (unsigned)got);
    return 0;
}
