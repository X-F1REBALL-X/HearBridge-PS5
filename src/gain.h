/* Software gain with a soft limiter (no wraparound, no hard clip).
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_GAIN_H
#define HEARBRIDGE_GAIN_H

#include <stdint.h>

#define HB_LIMIT_KNEE 0.80f   /* linear below this, soft curve above, max < 1 */

/* Parse a gain file text ("4", "2.5"): returns percent (0..500), or -1. */
int  gain_parse_pct(const char *text);
/* Format percent as the gain file text ("4.00\n"). */
void gain_format(int pct, char *out, int max);
/* pcm *= gain_milli/1000 with the soft limiter. Returns output peak x1000. */
int  gain_apply_soft(int16_t *pcm, int samples, int gain_milli);

struct hb_eq_s;
/* Same with the equalizer between gain and limiter (interleaved stereo,
 * samples = frames * 2): pcm * gain * EQ preamp -> EQ -> soft limiter.
 * eq NULL or off: exactly gain_apply_soft(). */
int  gain_apply_soft_eq(int16_t *pcm, int samples, int gain_milli, void *eq);

#endif
