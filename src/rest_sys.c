/* Developed by X-F1REBALL-X.
 * SystemStateMgr signals its transitions on the "SceSystemStateMgrStatus"
 * event flag; bit 0x400 is the rest mode (standby) request. We only poll it
 * without clearing, so the system's own handling is not touched. */
#include "rest_sys.h"
#include "diag.h"
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
    static const char *const libs[] = { "libkernel_sys.sprx", "libkernel.sprx", "libkernel_web.sprx" };
    fn_open op = 0;
    uint32_t h = 0;
    unsigned i;
    if (g_init) return;
    g_init = 1;
    for (i = 0; i < sizeof libs / sizeof libs[0] && !op; i++) {
        h = 0;
        if (kernel_dynlib_handle(-1, libs[i], &h) != 0 || !h) continue;
        op = (fn_open)kernel_dynlib_resolve(-1, h, NID_OpenEventFlag);
        g_poll = (fn_poll)kernel_dynlib_resolve(-1, h, NID_PollEventFlag);
    }
    if (op && g_poll && op(&g_ef, SSM_FLAG_NAME) == 0 && g_ef) g_ok = 1;
    diag_set("rest mode watch", "%s", g_ok ? "on (system state flag)"
             : op ? "off: system state flag not open" : "off: event flag calls not found");
    log_line("rest: watch %s", g_ok ? "on" : "off — resume is still handled after the wake");
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
