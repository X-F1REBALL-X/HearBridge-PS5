/* creds.h - temporarily take another authid (and full capabilities) for a
 * system call that checks who is calling, then put the original back.
 * Console only (needs the payload SDK kernel helpers). */
#ifndef HEARBRIDGE_CREDS_H
#define HEARBRIDGE_CREDS_H

#include <stdint.h>

/* ShellCore: what websrv/ftpsrv use around sceAppInstUtil*. */
#define HB_AUTHID_SHELLCORE 0x4801000000000013ULL

typedef struct {
    uint64_t authid;
    uint8_t  caps[16];
    int      have_caps;
    int      saved;
} hb_creds;

/* Switch to `authid` with all capability bits set. With `saved` non-NULL
 * the previous authid/caps are stored there for creds_restore(). `tag`
 * prefixes the log line. Returns the authid now in effect. */
uint64_t creds_elevate(uint64_t authid, hb_creds *saved, const char *tag);

/* Put back what creds_elevate() saved (no-op if nothing was saved). */
void creds_restore(hb_creds *saved, const char *tag);

#endif
