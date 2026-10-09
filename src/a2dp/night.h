/* Night mode: a gentle dynamic range compressor on the game audio before
 * the equalizer / gain / limiter and the SBC encoder. Loud bits (explosions)
 * come down, quiet bits (dialogue, footsteps) come up; silence stays silent.
 * Interleaved stereo s16, stereo-linked, pure C so the host tests run it.
 * Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_NIGHT_H
#define HEARBRIDGE_NIGHT_H

#include <stdint.h>

#define HB_NIGHT_THRESH_DB  (-30.f)  /* compress above this */
#define HB_NIGHT_RATIO        4.f
#define HB_NIGHT_MAKEUP_DB    9.f    /* lift for quiet sound */
#define HB_NIGHT_FLOOR_DB   (-62.f)  /* below this: no lift (keeps hiss down) */
#define HB_NIGHT_CEIL         0.89f  /* -1 dBFS output ceiling */

typedef struct {
    int   on;
    float env;      /* level follower (linear, 0..1) */
    float gain;     /* smoothed gain (linear) */
    float a_env, r_env, a_g, r_g;   /* per-sample coefficients */
} hb_night;

void  hb_night_init(hb_night *n, int rate);
void  hb_night_set(hb_night *n, int on);   /* off -> on starts from unity */
/* Static curve: gain in dB for a level in dBFS (exported for the tests). */
float hb_night_curve_db(float level_db);
/* In place. Off: untouched. */
void  hb_night_process(hb_night *n, int16_t *pcm, int frames);
/* Gain right now in tenths of a dB (status / tests). */
int   hb_night_gain_db10(const hb_night *n);

#endif
