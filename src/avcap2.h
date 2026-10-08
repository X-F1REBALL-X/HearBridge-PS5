/* libSceAvcap2 audio capture (Remote Play–style mix) for HearBridge.
 *
 * OpenAudio ABI + 0x38 param layout.
 */
#ifndef HEARBRIDGE_AVCAP2_H
#define HEARBRIDGE_AVCAP2_H

#include <stddef.h>
#include <stdint.h>

/* Streaming session for A2DP encode path (48 kHz stereo). */
typedef struct avcap2_session avcap2_session;

avcap2_session *avcap2_session_open(void);
void            avcap2_session_close(avcap2_session *s);

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
