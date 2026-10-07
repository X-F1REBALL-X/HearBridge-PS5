/* SBC encode API for HearBridge A2DP Source (implemented in sbc_enc.c,
 * written from the A2DP spec). Developed by X-F1REBALL-X. */
#ifndef HEARBRIDGE_SBC_H
#define HEARBRIDGE_SBC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int sample_rate;    /* 44100 or 48000 */
    int channels;       /* 1 or 2 */
    int bitpool;
    int blocks;         /* 4,8,12,16 — 0 = 16 */
    int subbands;       /* 4 or 8 — 0 = 8 */
    int allocation;     /* 0=loudness, 1=SNR */
    int joint_stereo;   /* 1 = joint stereo */
    /* Optional: raw 4-byte A2DP SBC codec IE (preferred if non-zero). */
    uint8_t a2dp_ie[4];
    int have_a2dp_ie;
} sbc_config;

typedef struct sbc_encoder sbc_encoder;

sbc_encoder *sbc_encoder_open(const sbc_config *cfg);
void         sbc_encoder_close(sbc_encoder *e);

/* Encodes interleaved s16le PCM. Returns encoded bytes written to out,
 * 0 if need more input, -1 on error. `frames` = samples per channel. */
int sbc_encoder_encode(sbc_encoder *e, const int16_t *pcm, int frames,
                       unsigned char *out, size_t out_max);

size_t sbc_encoder_frame_bytes(const sbc_encoder *e);
/* Changes the bitpool for the next frames (clamped to the mode's limit).
 * Returns the bitpool in use, -1 on error. */
int    sbc_encoder_set_bitpool(sbc_encoder *e, int bitpool);
int    sbc_encoder_bitpool(const sbc_encoder *e);
size_t sbc_encoder_codesize(const sbc_encoder *e); /* PCM bytes per SBC frame */
int    sbc_encoder_frame_samples(const sbc_encoder *e); /* samples/ch per frame */

#endif
