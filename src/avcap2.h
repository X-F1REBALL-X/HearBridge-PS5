/* libSceAvcap2 audio capture (Remote Play–style mix) for HearBridge.
 *
 * OpenAudio ABI + 0x38 param layout.
 */
#ifndef HEARBRIDGE_AVCAP2_H
#define HEARBRIDGE_AVCAP2_H

#include <stddef.h>
#include <stdint.h>

/* Capture roughly CAPTURE_SECONDS of audio into out_path (raw float32 LE stereo).
 * Logs rate/frames/amplitude stats. Returns 0 on success, non-zero on failure. */
int avcap2_capture_to_file(const char *out_path, int capture_seconds);

/* Streaming session for A2DP encode path (48 kHz stereo). */
typedef struct avcap2_session avcap2_session;

avcap2_session *avcap2_session_open(void);
void            avcap2_session_close(avcap2_session *s);

/* Read up to max_frames stereo frames into interleaved s16le.
 * Returns frames read (>0), 0 if empty/transient, -1 on hard error.
 * If peak_abs != NULL, writes max |sample| in [0,1] for this chunk (0 if empty). */
int avcap2_session_read_s16(avcap2_session *s, int16_t *out, int max_frames,
                            float *peak_abs);

#endif
