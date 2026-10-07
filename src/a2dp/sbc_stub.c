#include "sbc.h"
#include "log.h"

sbc_encoder *sbc_encoder_open(const sbc_config *cfg)
{
    (void)cfg;
    log_line("sbc: stub — encoder not linked (build sbc_enc.c)");
    return NULL;
}

void sbc_encoder_close(sbc_encoder *e) { (void)e; }

int sbc_encoder_encode(sbc_encoder *e, const int16_t *pcm, int frames,
                       unsigned char *out, size_t out_max)
{
    (void)e; (void)pcm; (void)frames; (void)out; (void)out_max;
    return -1;
}

size_t sbc_encoder_frame_bytes(const sbc_encoder *e)
{
    (void)e;
    return 0;
}
