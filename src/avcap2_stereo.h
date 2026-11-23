/* libSceAvcap2 audio capture (Remote Play–style 48 kHz stereo mix).
 *
 * This is HearBridge's default capture backend. It opens the system
 * av_capture_manager through libSceAvcap2 and reads the same stereo mix
 * the console sends to the TV / Remote Play, with no kernel access.
 *
 * The public capture API lives in avcap2.h; avcap2.c dispatches to this
 * backend unless the experimental kernel tap (ktap.h) is enabled.
 */
#ifndef HEARBRIDGE_AVCAP2_STEREO_H
#define HEARBRIDGE_AVCAP2_STEREO_H

#include <stddef.h>
#include <stdint.h>

typedef struct avcap2_stereo_session avcap2_stereo_session;

avcap2_stereo_session *avcap2_stereo_open(void);
void                   avcap2_stereo_close(avcap2_stereo_session *s);
int                    avcap2_stereo_probe(void);
int                    avcap2_stereo_read_s16(avcap2_stereo_session *s, int16_t *out,
                                              int max_frames, float *peak_abs);

#endif
