/* Host test for the HearBridge SBC encoder: encodes a stereo sine pair and
 * writes raw SBC + reference PCM; checks frame headers and CRC-8 with an
 * independent byte-wise table CRC. Build:
 *   cc -O2 -Isrc -Isrc/a2dp tests/test_sbc_enc.c src/a2dp/sbc_enc.c -lm
 * Usage: test_sbc_enc out.sbc ref.s16 [bitpool] [subbands] [blocks] [joint] [snr] */
#include "sbc.h"
#include "sbc_enc.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void log_line(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}

/* Reference CRC: MSB-first, poly 0x1D, init 0x0F, arbitrary bit length. */
static unsigned ref_crc(const unsigned char *d, int nbits)
{
    unsigned crc = 0x0F; int i;
    for (i = 0; i < nbits; i++) {
        unsigned bit = (d[i / 8] >> (7 - i % 8)) & 1;
        unsigned top = (crc >> 7) & 1;
        crc = (crc << 1) & 0xFF;
        if (top != bit) crc ^= 0x1D;
    }
    return crc;
}

static int bit_at(const unsigned char *d, int i) { return (d[i / 8] >> (7 - i % 8)) & 1; }

int main(int argc, char **argv)
{
    sbc_config cfg;
    sbc_encoder *e;
    int fs = 44100; const int secs = 3; int n;
    int16_t *pcm;
    unsigned char out[4096];
    FILE *fo, *fr;
    int i, frames = 0, bad = 0;
    size_t flen;

    if (argc < 3) return 2;
    if (getenv("SBC_FS")) fs = atoi(getenv("SBC_FS"));
    n = fs * secs;
    memset(&cfg, 0, sizeof cfg);
    cfg.sample_rate = fs; cfg.channels = 2;
    cfg.bitpool = argc > 3 ? atoi(argv[3]) : 53;
    cfg.subbands = argc > 4 ? atoi(argv[4]) : 8;
    cfg.blocks = argc > 5 ? atoi(argv[5]) : 16;
    cfg.joint_stereo = argc > 6 ? atoi(argv[6]) : 1;
    cfg.allocation = argc > 7 ? atoi(argv[7]) : 0;

    /* A2DP IE path sanity: 44.1k/joint, 16 blk/8 sb/loudness, 2..53 */
    {
        sbc_config c2 = cfg; sbc_encoder *e2;
        c2.have_a2dp_ie = 1;
        c2.a2dp_ie[0] = 0x21; c2.a2dp_ie[1] = 0x15; c2.a2dp_ie[2] = 2; c2.a2dp_ie[3] = 53;
        c2.bitpool = 0;
        e2 = sbc_encoder_open(&c2);
        if (!e2 || sbc_encoder_frame_bytes(e2) != 119) { fprintf(stderr, "IE path FAIL\n"); return 1; }
        sbc_encoder_close(e2);
    }

    e = sbc_encoder_open(&cfg);
    if (!e) return 1;
    flen = sbc_encoder_frame_bytes(e);
    pcm = malloc(sizeof *pcm * 2 * n);
    for (i = 0; i < n; i++) {
        double t = (double)i / fs;
        if (getenv("SBC_SINE1K")) {
            pcm[2 * i] = pcm[2 * i + 1] = (int16_t)lrint(16000.0 * sin(2 * M_PI * 1000.0 * t));
        } else {
        pcm[2 * i]     = (int16_t)lrint(12000.0 * sin(2 * M_PI * 1000.0 * t) + 3000.0 * sin(2 * M_PI * 6300.0 * t));
        pcm[2 * i + 1] = (int16_t)lrint(10000.0 * sin(2 * M_PI * 523.25 * t) + 2500.0 * sin(2 * M_PI * 11025.0 * t));
        }
    }
    fo = fopen(argv[1], "wb"); fr = fopen(argv[2], "wb");
    fwrite(pcm, 2, 2 * (size_t)n, fr); fclose(fr);

    for (i = 0; i < n; ) {
        int chunk = (n - i) < 1024 ? (n - i) : 1024, k, got;
        got = sbc_encoder_encode(e, pcm + 2 * i, chunk, out, sizeof out);
        if (got < 0) return 1;
        for (k = 0; k < got; k += (int)flen) {
            unsigned char *f = out + k, tmp[16];
            int mode = (f[1] >> 2) & 3, nsb = (f[1] & 1) ? 8 : 4, nch = mode ? 2 : 1;
            int hdr = 16 + (mode == 3 ? nsb : 0) + 4 * nsb * nch, b;
            memset(tmp, 0, sizeof tmp);
            for (b = 0; b < hdr; b++) {
                int src = b < 16 ? 8 + b : 32 + (b - 16);
                if (bit_at(f, src)) tmp[b / 8] |= (unsigned char)(0x80 >> (b % 8));
            }
            if (((f[1] >> 6) & 3) != (fs == 48000 ? 3 : fs == 44100 ? 2 : 1)) bad++;
            if (f[0] != 0x9C || f[2] != cfg.bitpool || f[3] != ref_crc(tmp, hdr) ||
                 nsb != cfg.subbands ||
                ((f[1] >> 4) & 3) != cfg.blocks / 4 - 1 ||
                mode != (cfg.joint_stereo ? 3 : 2))
                bad++;
            frames++;
        }
        fwrite(out, 1, (size_t)got, fo);
        i += chunk;
    }
    fclose(fo);
    printf("frames=%d frame_len=%zu header/crc_bad=%d\n", frames, flen, bad);
    sbc_encoder_close(e);
    free(pcm);
    return bad ? 1 : 0;
}
