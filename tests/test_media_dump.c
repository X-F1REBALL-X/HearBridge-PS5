/* Host test: encode a 1 kHz tone with the console's SBC config, pack it into
 * media packets with the console's builder, and write media_dump.bin in the
 * console format for tests/decode_dump.py. */
#include "avdtp.h"
#include "hsprefs.h"
#include "sbc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void log_line(const char *fmt, ...) { (void)fmt; }

int main(int argc, char **argv)
{
    /* What pick_sbc_config() selects for caps 3f ff, bitpool 2-53. */
    static const uint8_t ie_sbc[4] = { 0x11, 0x15, 35, 35 };
    /* SBC-XQ: 48 kHz dual channel, bitpool 38 per channel. */
    static const uint8_t ie_xq[4] = { 0x14, 0x15, 38, 38 };
    int xq = argc > 3 && !strcmp(argv[3], "xq");
    const uint8_t *ie = xq ? ie_xq : ie_sbc;
    avdtp_session s;
    sbc_config cfg;
    sbc_encoder *e;
    int16_t pcm[128 * 2];
    unsigned char frames[15 * 128], pkt[2048];
    int per_pkt = 4, fsz, i, t = 0, npk = argc > 2 ? atoi(argv[2]) : 200;

    if (argc < 2) return 2;
    {
        /* Format choice: 48 kHz two-channel only; refuse the rest. */
        int joint, bad = 0;
        const char *why;
        if (avdtp_sbc_pick_mode(0x3f, &joint, &why) != 0x11 || !joint) bad |= 1;     /* everything: 48k joint */
        if (avdtp_sbc_pick_mode(0x12, &joint, &why) != 0x12 || joint) bad |= 2;      /* 48k stereo */
        if (avdtp_sbc_pick_mode(0x14, &joint, &why) != 0x14 || joint) bad |= 4;      /* 48k dual */
        if (avdtp_sbc_pick_mode(0x2f, &joint, &why) != 0 || !why || !strstr(why, "44.1")) bad |= 8;  /* 44.1k only */
        if (avdtp_sbc_pick_mode(0x18, &joint, &why) != 0 || !why || !strstr(why, "mono")) bad |= 16; /* mono only */
        if (avdtp_sbc_pick_mode(0xc3, &joint, &why) != 0 || !why) bad |= 32;         /* 16/32 kHz only */
        printf("%s   SBC format: 48 kHz stereo accepted, 44.1 kHz-only / mono-only / other refused%s\n",
               bad ? "FAIL" : "ok", bad ? " (see bits)" : "");
        if (bad) { printf("FAIL bits %#x\n", bad); return 1; }
    }
    {
        /* Codec pick. The Xbox headset reports 3f ff, bitpool 2-60. */
        static const uint8_t xbox[4] = { 0x3f, 0xff, 2, 60 };
        static const uint8_t nodual[4] = { 0x33, 0xff, 2, 53 };   /* 48/44.1k, stereo+joint */
        static const uint8_t lowbp[4] = { 0x3f, 0xff, 2, 32 };
        avdtp_codec_pick c;
        const char *why;
        int bad = 0;
        if (!avdtp_sbc_pick(xbox, HB_CODEC_AUTO, 0, &c, &why) || c.codec != HB_CODEC_SBC_XQ ||
            c.cfg[0] != 0x14 || c.ceil != HB_XQ_CEIL || c.cfg[3] != HB_XQ_CEIL || c.start_bp != 35 ||
            c.avail != 0x0E) bad |= 1;                    /* auto -> XQ, all three available */
        if (!avdtp_sbc_pick(xbox, HB_CODEC_AUTO, 1, &c, &why) || c.codec != HB_CODEC_SBC_HQ || c.ceil != 60 ||
            c.cfg[0] != 0x11) bad |= 2;                   /* XQ failed before -> HQ */
        if (!avdtp_sbc_pick(xbox, HB_CODEC_SBC, 0, &c, &why) || c.codec != HB_CODEC_SBC || c.ceil != 53 ||
            c.cfg[3] != 53 || c.cfg[2] != 2 || c.cfg[1] != 0x15) bad |= 4;   /* plain SBC: 53 */
        if (!avdtp_sbc_pick(nodual, HB_CODEC_AUTO, 0, &c, &why) || c.codec != HB_CODEC_SBC || c.avail != 0x02)
            bad |= 8;                                     /* no dual, max 53 -> SBC */
        if (!avdtp_sbc_pick(nodual, HB_CODEC_SBC_XQ, 0, &c, &why) || c.codec != HB_CODEC_SBC) bad |= 16;
        if (!avdtp_sbc_pick(lowbp, HB_CODEC_AUTO, 0, &c, &why) || c.codec != HB_CODEC_SBC || c.ceil != 32 ||
            c.start_bp != 32) bad |= 32;                  /* bitpool max 32: too low for XQ */
        if (avdtp_sbc_pick((const uint8_t[4]){ 0x18, 0xff, 2, 53 }, HB_CODEC_AUTO, 0, &c, &why)) bad |= 64;
        /* HQ / XQ need 16 blocks + 8 subbands advertised */
        if (!avdtp_sbc_pick((const uint8_t[4]){ 0x3f, 0xef, 2, 60 }, HB_CODEC_SBC_XQ, 0, &c, &why) ||
            c.avail != 0x02 || c.codec != HB_CODEC_SBC) bad |= 128;          /* no 16 blocks */
        if (!avdtp_sbc_pick((const uint8_t[4]){ 0x3f, 0xfb, 2, 60 }, HB_CODEC_SBC_HQ, 0, &c, &why) ||
            c.avail != 0x02 || c.codec != HB_CODEC_SBC) bad |= 256;          /* 4 subbands only */
        if (!avdtp_sbc_pick((const uint8_t[4]){ 0x15, 0x15, 2, 60 }, HB_CODEC_AUTO, 0, &c, &why) ||
            c.avail != 0x0E) bad |= 512;          /* 48k joint+dual, 16/8 loudness: all three */
        if (!avdtp_sbc_pick((const uint8_t[4]){ 0x3f, 0xff, 2, 34 }, HB_CODEC_SBC_XQ, 0, &c, &why) ||
            (c.avail & 0x08) || c.codec != HB_CODEC_SBC) bad |= 1024;     /* max 34 < 35: no XQ */
        printf("%s   codec pick: auto -> SBC-XQ on the Xbox caps, HQ/SBC fallbacks, HQ/XQ only with 16 blocks/8 subbands, refuse mono%s\n",
               bad ? "FAIL" : "ok", bad ? " (see bits)" : "");
        if (bad) { printf("FAIL bits %#x\n", bad); return 1; }
    }
    memset(&s, 0, sizeof s);
    memcpy(s.sbc_cfg, ie, 4);
    s.rtp_ssrc = 0x48524247; s.rtp_seq = 1;
    memset(&cfg, 0, sizeof cfg);
    cfg.sample_rate = 48000; cfg.channels = 2; cfg.bitpool = ie[2];
    memcpy(cfg.a2dp_ie, ie, 4); cfg.have_a2dp_ie = 1;
    e = sbc_encoder_open(&cfg);
    if (!e) return 1;
    fsz = (int)sbc_encoder_frame_bytes(e);
    if (!avdtp_dump_open(&s, argv[1], npk)) return 1;
    for (i = 0; i < npk; i++) {
        int k, n = 0, len;
        for (k = 0; k < per_pkt; k++) {
            int j;
            for (j = 0; j < 128; j++, t++)
                pcm[2 * j] = pcm[2 * j + 1] =
                    (int16_t)lrint(16383.0 * sin(2 * M_PI * 1000.0 * t / 48000.0));
            n += sbc_encoder_encode(e, pcm, 128, frames + n, sizeof frames - (size_t)n);
        }
        if (n != per_pkt * fsz) { fprintf(stderr, "encode size %d\n", n); return 1; }
        len = avdtp_build_media(&s, frames, n, 128 * per_pkt, per_pkt, pkt, sizeof pkt);
        if (len != 13 + n) return 1;
        avdtp_dump_packet(&s, pkt, len);
    }
    avdtp_dump_close(&s);
    sbc_encoder_close(e);
    printf("wrote %d packets, %d-byte frames (%s)\n", npk, fsz, xq ? "SBC-XQ dual channel" : "SBC joint stereo");
    return 0;
}
