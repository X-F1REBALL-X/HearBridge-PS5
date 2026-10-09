/* Developed by X-F1REBALL-X.
 * SystemStateMgr signals its transitions on the "SceSystemStateMgrStatus"
 * event flag; bit 0x400 is the rest mode (standby) request. We only poll it
 * without clearing, so the system's own handling is not touched. */
#include "rest_sys.h"
#include "diag.h"
#include "dynmod.h"
#include "log.h"

#include <ps5/kernel.h>

#include <stdint.h>

#define NID_OpenEventFlag  "1vDaenmJtyA"  /* sceKernelOpenEventFlag */
#define NID_PollEventFlag  "9lvj5DjHZiA"  /* sceKernelPollEventFlag */

#define SSM_FLAG_NAME   "SceSystemStateMgrStatus"
#define SSM_BIT_STANDBY 0x400ULL
#define EVF_WAITMODE_OR 0x02

typedef int (*fn_open)(void **ef, const char *name);
typedef int (*fn_poll)(void *ef, uint64_t bits, uint32_t mode, uint64_t *res);

static fn_poll g_poll;
static void *g_ef;
static int g_init, g_ok;

static void init(void)
{
    fn_open op;
    uint32_t h1 = 0, h2 = 0;
    int rv = -1;
    if (g_init) return;
    g_init = 1;
    op = (fn_open)hb_kernel_sym(NID_OpenEventFlag, "sceKernelOpenEventFlag", &h1);
    g_poll = (fn_poll)hb_kernel_sym(NID_PollEventFlag, "sceKernelPollEventFlag", &h2);
    if (!op || !g_poll) {
        /* not in the libkernel we see by name: ask the loader for libkernel_sys */
        char how[128];
        uint32_t h = hb_mod_open("libkernel_sys.sprx", how, sizeof how);
        log_line("rest: libkernel_sys.sprx %s", how);
        if (!op) { op = (fn_open)hb_mod_sym(h, NID_OpenEventFlag, "sceKernelOpenEventFlag"); h1 = h; }
        if (!g_poll) { g_poll = (fn_poll)hb_mod_sym(h, NID_PollEventFlag, "sceKernelPollEventFlag"); h2 = h; }
    }
    log_line("rest: open event flag %p (handle %#x), poll %p (handle %#x)", (void *)op, h1, (void *)g_poll, h2);
    if (op && g_poll) {
        rv = op(&g_ef, SSM_FLAG_NAME);
        g_ok = rv == 0 && g_ef;
        log_line("rest: open %s -> %#x", SSM_FLAG_NAME, (unsigned)rv);
    }
    if (g_ok)
        diag_set("rest mode watch", "on (system state flag; open %p h %#x, poll %p h %#x)", (void *)op, h1, (void *)g_poll, h2);
    else if (op && g_poll)
        diag_set("rest mode watch", "off: system state flag not open (%#x; open %p h %#x, poll %p h %#x)",
                 (unsigned)rv, (void *)op, h1, (void *)g_poll, h2);
    else
        diag_set("rest mode watch", "off: event flag calls not found (open %p h %#x, poll %p h %#x)",
                 (void *)op, h1, (void *)g_poll, h2);
    log_line("rest: watch %s", g_ok ? "on" : "off, resume is still handled after the wake");
}

int hb_rest_sys_avail(void)
{
    init();
    return g_ok;
}

int hb_rest_sys_going_down(void)
{
    uint64_t res = 0;
    init();
    if (!g_ok) return 0;
    return g_poll(g_ef, SSM_BIT_STANDBY, EVF_WAITMODE_OR, &res) == 0 && (res & SSM_BIT_STANDBY);
}
