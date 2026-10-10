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

typedef int (*fn_modlist)(uint32_t *list, size_t max, size_t *count);

intptr_t hb_mod_scan(const char *nid, const char *name, uint32_t *h_out, int *n_out)
{
    static uint32_t list[256];
    fn_modlist ml = (fn_modlist)hb_kernel_sym(NULL, "sceKernelGetModuleList", NULL);
    size_t n = 0, i;
    if (n_out) *n_out = -1;
    if (!ml) {
        log_line("module: no sceKernelGetModuleList");
        return 0;
    }
    if (ml(list, sizeof list / sizeof list[0], &n) != 0) n = 0;
    if (n > sizeof list / sizeof list[0]) n = sizeof list / sizeof list[0];
    if (n_out) *n_out = (int)n;
    for (i = 0; i < n; i++) {
        intptr_t a = hb_mod_sym(list[i], nid, name);
        if (a) {
            log_line("module: %s found in loaded module %#x (%d modules)", name ? name : nid, list[i], (int)n);
            if (h_out) *h_out = list[i];
            return a;
        }
    }
    log_line("module: %s not in any of %d loaded modules", name ? name : nid, (int)n);
    return 0;
}

uint32_t hb_mod_open(const char *basename, char *how, int max)
{
    static const char *const roots[] = { "/system/common/lib/", "/system/priv/lib/",
                                         "/system_ex/common_ex/lib/", "/system_ex/priv_ex/lib/", "" };
    fn_load_start load;
    char path[160], msg[128];
    uint32_t h = 0;
    unsigned i;
    int rv = 0, res = 0;

    {
        /* the loaded name can lack ".sprx" or carry ".native" */
        char alt[3][80], *dot;
        snprintf(alt[0], sizeof alt[0], "%s", basename);
        snprintf(alt[1], sizeof alt[1], "%s", basename);
        if ((dot = strstr(alt[1], ".sprx"))) *dot = 0;
        snprintf(alt[2], sizeof alt[2], "%s.native.sprx", alt[1]);
        for (i = 0; i < 3; i++) {
            h = 0;
            if (kernel_dynlib_handle(-1, alt[i], &h) == 0 && h) {
                snprintf(msg, sizeof msg, "found by name %s, handle %#x", alt[i], h);
                say(how, max, msg);
                log_line("module: %s %s", basename, msg);
                return h;
            }
        }
        log_line("module: %s not found by name (also tried %s, %s)", basename, alt[1], alt[2]);
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
