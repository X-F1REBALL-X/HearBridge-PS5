/* Developed by X-F1REBALL-X.
 * Registers the tile folder with the console's app installer so it shows on
 * the home screen; selecting it follows deeplinkUri into the browser.
 *
 * One build for every firmware: on EVERY start the files are rewritten and
 * the folder is registered again (no "already installed" shortcut, so an
 * icon deleted from the home screen always comes back). The per-title call
 * (sceAppInstUtilAppInstallTitleDir) is looked up at run time and
 * sceAppInstUtilAppInstallAll(0) is used when it is missing or fails. The
 * first attempt uses the payload's own rights (as in 1.0.2, tested on fw
 * 10.20); only if it fails is the installer called again as ShellCore (the
 * former fw13.60 build) and the original credentials restored. An error
 * code from re-registering an installed title counts as success when
 * sceAppInstUtilAppExists (or /user/appmeta/<id>) says the title is
 * installed. Every step is reported (log + diag.h). */
#include "tile.h"
#include "creds.h"
#include "diag.h"
#include "icon_png.h"
#include "start_html.h"
#include "log.h"

#include <ps5/kernel.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppUnInstall(const char *title_id);

#define NID_AppInstallTitleDir "Wudg3Xe3heE"  /* sceAppInstUtilAppInstallTitleDir */
#define NID_AppInstallAll      "+scQA5stvjs"  /* sceAppInstUtilAppInstallAll */
#define NID_AppExists          "kUT4RpxclMQ"  /* sceAppInstUtilAppExists */

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static void *appinst_nid(const char *nid)
{
    uint32_t h = 0;
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &h) != 0 || !h)
        return NULL;
    return (void *)kernel_dynlib_resolve(-1, h, nid);
}

static int meta_exists(const char *title_id)
{
    char p[64];
    snprintf(p, sizeof p, "/user/appmeta/%s", title_id);
    return is_dir(p);
}

static unsigned long long raise_creds(void *ctx)
{
    return creds_elevate(HB_AUTHID_SHELLCORE, (hb_creds *)ctx, "tile");
}

static void lower_creds(void *ctx)
{
    creds_restore((hb_creds *)ctx, "tile");
}

static void report(tile_report *r)
{
    char line[DIAG_VAL_MAX];
    tile_report_line(r, line, sizeof line);
    log_line("tile: %s", line);
    diag_set("tile", "%s", line);
    diag_set("tile appmeta", "%s %s", HB_TILE_APPMETA, r->meta_after ? "exists" : "missing");
    (void)diag_save();
}

int tile_install(const char *url, tile_report *rep)
{
    tile_report local;
    tile_report *r = rep ? rep : &local;
    tile_ops ops;
    hb_creds saved;

    memset(r, 0, sizeof *r);
    if (!url || !url[0]) url = HB_TILE_URL;
    diag_set("tile url", "%s", url);

    r->legacy_marker = unlink(HB_TILE_LEGACY_MARK) == 0;
    errno = 0;
    if (tile_files_write(HB_TILE_ROOT, url, hb_icon_png, sizeof hb_icon_png,
                         hb_start_html, HB_START_HTML_LEN) != 0) {
        r->files_failed = 1;
        r->files_errno = errno;
        log_line("tile: cannot write %s/%s (errno %d)", HB_TILE_ROOT, HB_TILE_ID, r->files_errno);
        r->result = -1;
        r->code = r->files_errno;
        r->failed = "writing " HB_TILE_ROOT "/" HB_TILE_ID;
        report(r);
        return -1;
    }

    memset(&ops, 0, sizeof ops);
    ops.init = sceAppInstUtilInitialize;
    ops.term = sceAppInstUtilTerminate;
    ops.title_dir = (int (*)(const char *, const char *, void *))appinst_nid(NID_AppInstallTitleDir);
    ops.install_all = (int (*)(void *))appinst_nid(NID_AppInstallAll);
    ops.app_exists = (int (*)(const char *, int *))appinst_nid(NID_AppExists);
    ops.meta_exists = meta_exists;

    r->authid_before = kernel_get_ucred_authid(-1);
    (void)tile_register_fallback(&ops, r, raise_creds, lower_creds, &saved);

    log_line("tile: register %s -> %s (opens %s)", HB_TILE_ID,
             r->result ? "FAILED" : r->already ? "ok (was already installed)" : "ok", url);
    report(r);
    return r->result;
}

int tile_uninstall(void)
{
    hb_creds saved;
    int rc;

    rc = sceAppInstUtilInitialize();
    if (!rc) {
        rc = sceAppInstUtilAppUnInstall(HB_TILE_ID);
        sceAppInstUtilTerminate();
    }
    if (rc) {
        /* Own rights refused (newer firmware): once more as ShellCore. */
        creds_elevate(HB_AUTHID_SHELLCORE, &saved, "tile");
        rc = sceAppInstUtilInitialize();
        if (!rc) {
            rc = sceAppInstUtilAppUnInstall(HB_TILE_ID);
            sceAppInstUtilTerminate();
        }
        creds_restore(&saved, "tile");
    }
    log_line("tile: unregister %s -> %#x", HB_TILE_ID, (unsigned)rc);
    diag_set("tile", "removed on request (unregister %#x)", (unsigned)rc);
    unlink(HB_TILE_LEGACY_MARK);
    tile_files_drop(HB_TILE_ROOT);
    return rc ? -1 : 0;
}
