/* A2DP media packet builder + debug dump. No link dependency, so the host
 * tests build the exact bytes the console sends. Developed by X-F1REBALL-X. */
#include "avdtp.h"

#include <stdio.h>
#include <string.h>

int avdtp_sbc_pick_mode(unsigned char caps0, int *joint, const char **why)
{
    unsigned char out = 0x10;                     /* 48 kHz */
    *joint = 0;
    *why = NULL;
    /* freq: bit7=16k bit6=32k bit5=44.1 bit4=48 */
    if (!(caps0 & 0x10)) {
        *why = (caps0 & 0x20) ? "the headset takes 44.1 kHz but not 48 kHz (no resampler)"
                              : "the headset takes neither 44.1 nor 48 kHz";
        return 0;
    }
    /* channel: bit3=mono bit2=dual bit1=stereo bit0=joint */
    if (caps0 & 0x01) { out |= 0x01; *joint = 1; }
    else if (caps0 & 0x02) out |= 0x02;
    else if (caps0 & 0x04) out |= 0x04;
    else {
        *why = (caps0 & 0x08) ? "the headset takes mono only (no downmix)"
                              : "the headset takes no stereo mode";
        return 0;
    }
    return out;
}

int avdtp_build_media(avdtp_session *s, const unsigned char *sbc_frames, int len,
                      int samples_in_packet, int n_sbc_frames,
                      unsigned char *pkt, int pkt_max)
{
    int nf = n_sbc_frames > 0 ? n_sbc_frames : 1;
    if (!s || len <= 0 || 13 + len > pkt_max) return 0;
    if (nf > 15) nf = 15;              /* 4-bit NUM field, non-fragmented */
    /* RTP (RFC 3550): V=2 P=0 X=0 CC=0 | M=0 PT=96 | seq | ts | SSRC */
    pkt[0] = 0x80;
    pkt[1] = 0x60;
    pkt[2] = (unsigned char)(s->rtp_seq >> 8);
    pkt[3] = (unsigned char)s->rtp_seq;
    pkt[4] = (unsigned char)(s->rtp_ts >> 24);
    pkt[5] = (unsigned char)(s->rtp_ts >> 16);
    pkt[6] = (unsigned char)(s->rtp_ts >> 8);
    pkt[7] = (unsigned char)s->rtp_ts;
    pkt[8] = (unsigned char)(s->rtp_ssrc >> 24);
    pkt[9] = (unsigned char)(s->rtp_ssrc >> 16);
    pkt[10] = (unsigned char)(s->rtp_ssrc >> 8);
    pkt[11] = (unsigned char)s->rtp_ssrc;
    /* A2DP SBC media payload header: F=S=L=RFA=0, NUM = frame count */
    pkt[12] = (unsigned char)(nf & 0x0F);
    memcpy(pkt + 13, sbc_frames, (size_t)len);
    s->rtp_seq++;
    s->rtp_ts += (uint32_t)(samples_in_packet > 0 ? samples_in_packet : 128);
    return 13 + len;
}

/* Dump format: "HBMD1\n" magic, 4-byte SBC IE (as configured), then per
 * packet a little-endian u16 length + the L2CAP payload (RTP included). */
int avdtp_dump_open(avdtp_session *s, const char *path, int max_pkts)
{
    FILE *f;
    if (!s || !path) return 0;
    f = fopen(path, "wb");
    if (!f) return 0;
    fwrite("HBMD1\n", 1, 6, f);
    fwrite(s->sbc_cfg, 1, 4, f);
    s->dump = f;
    s->dump_left = max_pkts;
    return 1;
}

void avdtp_dump_packet(avdtp_session *s, const unsigned char *p, int n)
{
    unsigned char h[2];
    FILE *f;
    if (!s || !s->dump || n <= 0) return;
    f = (FILE *)s->dump;
    h[0] = (unsigned char)n;
    h[1] = (unsigned char)(n >> 8);
    fwrite(h, 1, 2, f);
    fwrite(p, 1, (size_t)n, f);
    if (--s->dump_left <= 0) avdtp_dump_close(s);
}

void avdtp_dump_close(avdtp_session *s)
{
    if (s && s->dump) {
        fclose((FILE *)s->dump);
        s->dump = NULL;
    }
}
