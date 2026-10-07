/* stop.c - see stop.h. */
#include "stop.h"
#include "log.h"
#include "util.h"

#include <signal.h>
#include <unistd.h>

#define STOP_POLL_MS 500

static volatile sig_atomic_t g_stop;
static long g_last_check = -1;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

int hb_stop_requested(void)
{
    long t;
    if (g_stop) return 1;
    t = now_ms();
    if (g_last_check >= 0 && t - g_last_check < STOP_POLL_MS) return 0;
    g_last_check = t;
    if (access(HB_STOP_PATH, F_OK) == 0) {
        g_stop = 1;
        log_line("stop: %s found — shutting down", HB_STOP_PATH);
    }
    return g_stop ? 1 : 0;
}

void hb_stop_request(void)
{
    g_stop = 1;
}

void hb_stop_init(void)
{
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGHUP, on_signal);
    unlink(HB_STOP_PATH);
}

void hb_stop_clear(void)
{
    unlink(HB_STOP_PATH);
}
