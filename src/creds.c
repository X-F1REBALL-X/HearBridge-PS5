/* creds.c - see creds.h. Developed by X-F1REBALL-X. */
#include "creds.h"
#include "log.h"

#include <ps5/kernel.h>

#include <string.h>

uint64_t creds_elevate(uint64_t authid, hb_creds *saved, const char *tag)
{
    uint8_t caps[16];
    uint64_t before, now;

    before = kernel_get_ucred_authid(-1);
    if (saved) {
        memset(saved, 0, sizeof *saved);
        saved->authid = before;
        saved->have_caps = kernel_get_ucred_caps(-1, saved->caps) == 0;
        saved->saved = 1;
    }
    memset(caps, 0xff, sizeof caps);
    kernel_set_ucred_authid(-1, authid);
    kernel_set_ucred_caps(-1, caps);
    now = kernel_get_ucred_authid(-1);
    log_line("%s: authid %#llx -> %#llx (wanted %#llx)", tag ? tag : "creds",
             (unsigned long long)before, (unsigned long long)now,
             (unsigned long long)authid);
    return now;
}

void creds_restore(hb_creds *saved, const char *tag)
{
    if (!saved || !saved->saved) return;
    kernel_set_ucred_authid(-1, saved->authid);
    if (saved->have_caps) kernel_set_ucred_caps(-1, saved->caps);
    saved->saved = 0;
    log_line("%s: authid restored to %#llx%s", tag ? tag : "creds",
             (unsigned long long)kernel_get_ucred_authid(-1),
             saved->have_caps ? "" : " (caps not restored: could not read them)");
}
