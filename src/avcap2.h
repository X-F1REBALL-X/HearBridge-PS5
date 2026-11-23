/* Audio capture for HearBridge (A2DP encode path, 48 kHz stereo).
 *
 * This is the stable capture interface. It dispatches to one of two
 * backends (see avcap2.c): the default libSceAvcap2 stereo mix
 * (avcap2_stereo.h) or, opt-in, the experimental kernel tap (ktap.h).
 */
#ifndef HEARBRIDGE_AVCAP2_H
#define HEARBRIDGE_AVCAP2_H

#include <stddef.h>
#include <stdint.h>

/* Streaming session for A2DP encode path (48 kHz stereo). */
typedef struct avcap2_session avcap2_session;

avcap2_session *avcap2_session_open(void);
void            avcap2_session_close(avcap2_session *s);

/* 1 if the experimental kernel tap backend is active for this run, else 0
 * (the default stereo mix). Decided once, at the first capture call. */
int avcap2_backend_is_ktap(void);

/* Diagnostics only: load libSceIpmi/libSceAvcap2 and check that the Avcap2
 * functions resolve (no capture is started). Results go to diag.h.
 * 0 = everything present. */
int avcap2_probe(void);

/* Read up to max_frames stereo frames into interleaved s16le.
 * Returns frames read (>0), 0 if empty/transient, -1 on hard error.
 * If peak_abs != NULL, writes max |sample| in [0,1] for this chunk (0 if empty). */
int avcap2_session_read_s16(avcap2_session *s, int16_t *out, int max_frames,
                            float *peak_abs);

#endif
