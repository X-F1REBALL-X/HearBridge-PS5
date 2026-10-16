/* stop.c - see stop.h. */
#include "stop.h"
#include "log.h"
#include "util.h"

#include <signal.h>
#include <unistd.h>

#define STOP_POLL_MS 500

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_sig;      /* last SIGTERM/SIGINT seen, not yet logged */
static volatile sig_atomic_t g_hup;      /* SIGHUPs seen, not yet logged */
static long g_last_check = -1;

static void on_signal(int sig)
{
    g_sig = sig;
    g_stop = 1;
}

/* SIGHUP is not a stop request: the console can send it when a connection or
 * session it tied the payload to goes away (e.g. the network link drops).
 * HearBridge keeps running; the event is only logged. */
static void on_hup(int sig)
{
    (void)sig;
    g_hup++;
}

int hb_stop_requested(void)
{
    long t;
    if (g_hup) {
        int n = g_hup;
        g_hup = 0;
        log_line("stop: SIGHUP received %d time(s), ignored (keeps running)", n);
    }
    if (g_sig) {
        int sg = g_sig;
        g_sig = 0;
        log_line("stop: signal %d received — shutting down", sg);
    }
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
    signal(SIGHUP, on_hup);
    signal(SIGPIPE, SIG_IGN);       /* a vanished network peer must not kill the payload */
    unlink(HB_STOP_PATH);
}

void hb_stop_clear(void)
{
    unlink(HB_STOP_PATH);
}
