/* sysinfo.c - see sysinfo.h. Developed by X-F1REBALL-X. */
#include "sysinfo.h"
#include "diag.h"
#include "log.h"

#include <ps5/kernel.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define NID_GetHwModelName              "JC7I7J1bllQ" /* sceKernelGetHwModelName */
#define NID_GetProsperoSystemSwVersion  "aML18Z0J0t0" /* sceKernelGetProsperoSystemSwVersion */

/* Layout used by ps5-payload-dev shsrv for the version call. */
typedef struct {
    uint64_t size;
    char     str_version[0x1c];
    uint32_t bin_version;
    uint64_t pad;
} sw_version;

void *sysinfo_libkernel_nid(const char *nid)
{
    static const char *const names[] = {
        "libkernel_web.sprx", "libkernel.sprx", "libkernel_sys.sprx",
    };
    size_t i;

    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        uint32_t h = 0;
        intptr_t a;
        if (kernel_dynlib_handle(-1, names[i], &h) != 0 || !h) continue;
        a = kernel_dynlib_resolve(-1, h, nid);
        if (a) return (void *)a;
    }
    return NULL;
}

void sysinfo_collect(void)
{
    int (*model_fn)(char *) = (int (*)(char *))sysinfo_libkernel_nid(NID_GetHwModelName);
    int (*ver_fn)(sw_version *) = (int (*)(sw_version *))sysinfo_libkernel_nid(NID_GetProsperoSystemSwVersion);
    uint32_t fw = kernel_get_fw_version();
    char model[1024];
    sw_version v;
    int rc;

    if (ver_fn) {
        memset(&v, 0, sizeof v);
        v.size = 0x28;
        rc = ver_fn(&v);
        v.str_version[sizeof v.str_version - 1] = 0;
        if (!rc)
            diag_set("firmware", "%s (system %#x, kernel %#x)", v.str_version,
                     (unsigned)v.bin_version, (unsigned)fw);
        else
            diag_set("firmware", "kernel %#x (system version call failed %#x)",
                     (unsigned)fw, (unsigned)rc);
    } else {
        diag_set("firmware", "kernel %#x (system version call not found)", (unsigned)fw);
    }

    memset(model, 0, sizeof model);
    if (!model_fn)
        diag_set("model", "unknown (sceKernelGetHwModelName not found)");
    else if ((rc = model_fn(model)) != 0)
        diag_set("model", "unknown (sceKernelGetHwModelName failed %#x)", (unsigned)rc);
    else {
        model[sizeof model - 1] = 0;
        diag_set("model", "%.60s", model);
    }

    diag_set("process", "pid %d, authid %#llx, uid %d, ruid %d",
             (int)getpid(), (unsigned long long)kernel_get_ucred_authid(-1),
             (int)kernel_get_ucred_uid(-1), (int)kernel_get_ucred_ruid(-1));
    log_line("sysinfo: fw kernel %#x, model %s, authid %#llx", (unsigned)fw,
             model[0] ? model : "?", (unsigned long long)kernel_get_ucred_authid(-1));
}
