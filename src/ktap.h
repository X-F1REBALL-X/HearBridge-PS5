/* Experimental kernel-tap capture (opt-in, off by default).
 *
 * Instead of the libSceAvcap2 stereo mix (avcap2_stereo.h), this backend
 * reads the uaudio0 playback frame buffer: the PCM the system audio server
 * hands to a USB headphone endpoint (PULSE Explore dongle / USB headset),
 * i.e. the post-Tempest binaural render, before the dongle's own radio link.
 *
 * Read-only: kernel_copyout reads only, page-table validated, no writes, no
 * MMIO, no locks, the producer is never stopped. Still experimental:
 *
 *   - It only carries 3D/binaural audio while a USB headphone endpoint is
 *     plugged in and selected as the console's audio output; with no such
 *     endpoint the buffer is silent or absent and open() fails.
 *   - The softc/buffer offsets are firmware-specific (validated on 12.40).
 *
 * Enabled only when avcap2.c sees the ktap sentinel file; the public capture
 * API in avcap2.h is unchanged and defaults to the stereo backend.
 */
#ifndef HEARBRIDGE_KTAP_H
#define HEARBRIDGE_KTAP_H

#include <stddef.h>
#include <stdint.h>

typedef struct ktap_session ktap_session;

ktap_session *ktap_open(void);
void          ktap_close(ktap_session *s);
int           ktap_probe(void);
int           ktap_read_s16(ktap_session *s, int16_t *out, int max_frames,
                            float *peak_abs);

#endif
