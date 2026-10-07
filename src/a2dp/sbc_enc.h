/* HearBridge SBC encoder core (A2DP 1.3 Appendix B / Section 12).
 * Written from the specification by X-F1REBALL-X. Encoder only, generic C.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HEARBRIDGE_SBC_ENC_H
#define HEARBRIDGE_SBC_ENC_H

#include <stddef.h>
#include <stdint.h>

/* Header field codes (spec Tables 12.16-12.20). */
enum { HB_SBC_FS_16K = 0, HB_SBC_FS_32K = 1, HB_SBC_FS_44K1 = 2, HB_SBC_FS_48K = 3 };
enum { HB_SBC_MONO = 0, HB_SBC_DUAL = 1, HB_SBC_STEREO = 2, HB_SBC_JOINT = 3 };
enum { HB_SBC_LOUDNESS = 0, HB_SBC_SNR = 1 };

typedef struct {
    int fs_code;        /* HB_SBC_FS_* */
    int mode;           /* HB_SBC_MONO.. */
    int alloc;          /* HB_SBC_LOUDNESS / HB_SBC_SNR */
    int nblocks;        /* 4, 8, 12, 16 */
    int nsub;           /* 4 or 8 */
    int bitpool;
    /* analysis history per channel, newest sample at index 0 */
    double hist[2][80];
} hb_sbc_state;

/* Validate parameters and reset filter state. 0 on success. */
int    hb_sbc_setup(hb_sbc_state *st);
int    hb_sbc_channels(const hb_sbc_state *st);
size_t hb_sbc_frame_len(const hb_sbc_state *st);   /* bytes */
size_t hb_sbc_pcm_bytes(const hb_sbc_state *st);   /* s16 bytes per frame */

/* Encode one frame: nblocks*nsub samples per channel, interleaved s16.
 * Returns frame bytes written, or -1. */
int hb_sbc_encode_frame(hb_sbc_state *st, const int16_t *pcm,
                        uint8_t *out, size_t out_max);

uint8_t hb_sbc_crc8(const uint8_t *buf, size_t nbits); /* CRC over leading bits */

#endif
