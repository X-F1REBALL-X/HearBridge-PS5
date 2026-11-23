/* HearBridge audio capture — backend dispatch.
 *
 * Two capture backends share the avcap2.h interface:
 *
 *   - avcap2_stereo (default): libSceAvcap2, the 48 kHz stereo TV / Remote
 *     Play mix, no kernel access. This is what ships and what runs unless
 *     the kernel tap is explicitly turned on.
 *
 *   - ktap (experimental, opt-in): a read-only tap of the uaudio0 playback
 *     buffer, which can carry the post-Tempest binaural render when a USB
 *     headphone endpoint is plugged in. See ktap.h for the caveats.
 *
 * The backend is chosen once, by the presence of a sentinel file, matching
 * the convention HearBridge already uses for optional/debug features
 * (TONE_PATH, HCI_DEBUG_PATH, ...). No file -> stereo mix, as before.
 */
#include "avcap2.h"
#include "avcap2_stereo.h"
#include "ktap.h"
#include "log.h"

#include <stdlib.h>
#include <unistd.h>

/* Create this file to enable the experimental kernel tap:
 *     touch /data/hearbridge/ktap   (then restart the payload)
 * Kept in sync with KTAP_PATH in main.c. */
#define KTAP_FLAG_PATH "/data/hearbridge/ktap"

struct avcap2_session {
    int                    ktap;
    avcap2_stereo_session *st;
    ktap_session          *kt;
};

/* Decided once and logged once; the probe and the session open must agree. */
static int backend_ktap(void)
{
    static int cached = -1;

    if (cached < 0) {
        cached = (access(KTAP_FLAG_PATH, F_OK) == 0) ? 1 : 0;
        log_line("capture: backend = %s",
                 cached ? "kernel tap (experimental, " KTAP_FLAG_PATH " present)"
                        : "libSceAvcap2 stereo mix (default)");
    }
    return cached;
}

int avcap2_backend_is_ktap(void)
{
    return backend_ktap();
}

int avcap2_probe(void)
{
    return backend_ktap() ? ktap_probe() : avcap2_stereo_probe();
}

avcap2_session *avcap2_session_open(void)
{
    avcap2_session *s = calloc(1, sizeof *s);

    if (!s)
        return NULL;
    s->ktap = backend_ktap();
    if (s->ktap)
        s->kt = ktap_open();
    else
        s->st = avcap2_stereo_open();
    if (!s->kt && !s->st) {
        free(s);
        return NULL;
    }
    return s;
}

void avcap2_session_close(avcap2_session *s)
{
    if (!s)
        return;
    if (s->kt)
        ktap_close(s->kt);
    if (s->st)
        avcap2_stereo_close(s->st);
    free(s);
}

int avcap2_session_read_s16(avcap2_session *s, int16_t *out, int max_frames,
                            float *peak_abs)
{
    if (peak_abs)
        *peak_abs = 0.f;
    if (!s)
        return -1;
    if (s->kt)
        return ktap_read_s16(s->kt, out, max_frames, peak_abs);
    if (s->st)
        return avcap2_stereo_read_s16(s->st, out, max_frames, peak_abs);
    return -1;
}
