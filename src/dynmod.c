/* Developed by X-F1REBALL-X. See dynmod.h. */
#include "dynmod.h"
#include "log.h"

#include <ps5/kernel.h>

#include <stdio.h>
#include <string.h>

#define NID_LoadStartModule "wzvqT4UqKX8"  /* sceKernelLoadStartModule */

typedef int (*fn_load_start)(const char *, size_t, const void *, uint32_t, void *, int *);

static const char *const g_kernels[] = { "libkernel_web.sprx", "libkernel.sprx", "libkernel_sys.sprx" };

intptr_t hb_mod_sym(uint32_t h, const char *nid, const char *name)
{
    intptr_t a = 0;
    if (!h) return 0;
    if (nid) a = kernel_dynlib_resolve(-1, h, nid);
    if (!a && name) a = kernel_dynlib_dlsym(-1, h, name);
    return a;
}

intptr_t hb_kernel_sym(const char *nid, const char *name, uint32_t *h_out)
{
    unsigned i;
    for (i = 0; i < sizeof g_kernels / sizeof g_kernels[0]; i++) {
        uint32_t h = 0;
        intptr_t a;
        if (kernel_dynlib_handle(-1, g_kernels[i], &h) != 0 || !h) continue;
        if ((a = hb_mod_sym(h, nid, name))) {
            if (h_out) *h_out = h;
            return a;
        }
    }
    return 0;
}

static void say(char *how, int max, const char *s)
{
    if (how && max > 0) snprintf(how, (size_t)max, "%s", s);
}

uint32_t hb_mod_open(const char *basename, char *how, int max)
{
    static const char *const roots[] = { "/system/common/lib/", "/system/priv/lib/", "" };
    fn_load_start load;
    char path[160], msg[128];
    uint32_t h = 0;
    unsigned i;
    int rv = 0, res = 0;

    if (kernel_dynlib_handle(-1, basename, &h) == 0 && h) {
        snprintf(msg, sizeof msg, "found by name, handle %#x", h);
        say(how, max, msg);
        log_line("module: %s %s", basename, msg);
        return h;
    }
    load = (fn_load_start)hb_kernel_sym(NID_LoadStartModule, "sceKernelLoadStartModule", NULL);
    if (!load) {
        say(how, max, "not found by name, no sceKernelLoadStartModule");
        log_line("module: %s not found by name and no sceKernelLoadStartModule", basename);
        return 0;
    }
    for (i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        snprintf(path, sizeof path, "%s%s", roots[i], basename);
        res = 0;
        rv = load(path, 0, NULL, 0, NULL, &res);
        log_line("module: LoadStart %s -> rv %#x res %#x", path, (unsigned)rv, (unsigned)res);
        h = 0;
        if (kernel_dynlib_handle(-1, basename, &h) == 0 && h) {
            snprintf(msg, sizeof msg, "loaded from %s, handle %#x", path, h);
            break;
        }
        /* A positive result is the module id, also for a module that was
         * already loaded under another name. */
        if (rv > 0) {
            h = (uint32_t)rv;
            snprintf(msg, sizeof msg, "loaded from %s, handle %#x (module id)", path, h);
            break;
        }
    }
    if (!h) snprintf(msg, sizeof msg, "not found (last rv %#x res %#x)", (unsigned)rv, (unsigned)res);
    say(how, max, msg);
    log_line("module: %s %s", basename, msg);
    return h;
}
