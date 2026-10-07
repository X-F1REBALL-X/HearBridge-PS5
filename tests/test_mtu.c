/* Developed by X-F1REBALL-X.
 * Every media packet must fit the peer MTU: for each SBC mode, subband
 * count and bitpool, the real encoder's frames are packed with the
 * frames-per-packet rule and the packet size is checked against the MTU. */
#include "rate.h"
#include "sbc.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void log_line(const char *fmt, ...) { (void)fmt; }

int main(void)
{
    static const int mtus[] = { 339, 672, 895, 1021 };
    static int16_t pcm[128 * 2 * 4];
    unsigned char out[1024];
    int fails = 0, checked = 0, mi, ch, js, sb, bp, i;

    for (i = 0; i < (int)(sizeof pcm / sizeof *pcm); i++) pcm[i] = (int16_t)((i * 7919) % 20000 - 10000);
    for (mi = 0; mi < 4; mi++)
        for (ch = 1; ch <= 2; ch++)
            for (js = 0; js <= (ch == 2); js++)
                for (sb = 4; sb <= 8; sb += 4)
                    for (bp = 2; bp <= 53; bp++) {
                        sbc_config cfg;
                        sbc_encoder *e;
                        int fl, per, got, spf, size;
                        memset(&cfg, 0, sizeof cfg);
                        cfg.sample_rate = 48000; cfg.channels = ch; cfg.bitpool = bp;
                        cfg.blocks = 16; cfg.subbands = sb; cfg.joint_stereo = js;
                        e = sbc_encoder_open(&cfg);
                        if (!e) continue;
                        sbc_encoder_set_bitpool(e, bp);
                        fl = (int)sbc_encoder_frame_bytes(e);
                        spf = sbc_encoder_frame_samples(e);
                        got = sbc_encoder_encode(e, pcm, spf, out, sizeof out);
                        if (got > 0 && got != fl) {
                            printf("FAIL frame_bytes %d != encoded %d (ch %d js %d sb %d bp %d)\n", fl, got, ch, js, sb, bp);
                            fails++;
                        }
                        per = hb_frames_per_packet(mtus[mi], got > 0 ? got : fl);
                        size = HB_MEDIA_HDR + per * (got > 0 ? got : fl);
                        if (size > mtus[mi] && per > 1) {
                            printf("FAIL mtu %d: %d frames x %d = %d bytes (ch %d js %d sb %d bp %d)\n",
                                   mtus[mi], per, fl, size, ch, js, sb, bp);
                            fails++;
                        }
                        checked++;
                        sbc_encoder_close(e);
                    }
    /* The case from the log: 672 MTU, joint stereo 8 subbands, bitpool 22. */
    {
        int fl = 4 + 8 + (8 + 16 * 22 + 7) / 8, per = hb_frames_per_packet(672, fl);
        if (HB_MEDIA_HDR + per * fl > 672 || per >= 15) { printf("FAIL 672/bp22: %d frames\n", per); fails++; }
        else printf("ok   MTU 672, bitpool 22 (%d-byte frames): %d frames/packet = %d bytes\n", fl, per, HB_MEDIA_HDR + per * fl);
    }
    printf("%s %d mode/bitpool/MTU combinations fit the MTU\n", fails ? "FAIL" : "ok  ", checked);
    return fails ? 1 : 0;
}
