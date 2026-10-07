#include "avdtp.h"
#include "log.h"

int avdtp_discover(void *link_ctx, avdtp_sink_info *out)
{
    (void)link_ctx; (void)out;
    log_line("avdtp: stub — discover not implemented");
    return 0;
}

int avdtp_configure_sbc(void *link_ctx, const avdtp_sink_info *sink, int bitpool)
{
    (void)link_ctx; (void)sink; (void)bitpool;
    log_line("avdtp: stub — configure not implemented");
    return 0;
}

int avdtp_open_start(void *link_ctx)
{
    (void)link_ctx;
    log_line("avdtp: stub — open/start not implemented");
    return 0;
}

int avdtp_send_media(void *link_ctx, const unsigned char *frame, int len)
{
    (void)link_ctx; (void)frame; (void)len;
    return 0;
}

int avdtp_close(void *link_ctx)
{
    (void)link_ctx;
    return 0;
}
