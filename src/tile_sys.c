/* Developed by X-F1REBALL-X.
 * Registers the tile folder with the console's app installer so it shows on
 * the home screen; selecting it follows deeplinkUri into the browser.
 *
 * 1.0.1-fw13.60: the per-title call (sceAppInstUtilAppInstallTitleDir) is
 * looked up at run time and sceAppInstUtilAppInstallAll(0) is used when it
 * is missing or fails (what ftpsrv/websrv/dump_installer do for newer
 * firmware); the installer runs as ShellCore and the original credentials
 * are restored afterwards; an earlier registration is only trusted while
 * /user/appmeta/<id> exists; every step is reported (log + diag.h). */
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

typedef int (*fn_title_dir)(const char *title_id, const char *dir, void *opt);
typedef int (*fn_install_all)(void *opt);

#define TILE_MARK HB_TILE_ROOT "/" HB_TILE_ID "/sce_sys/.hb_registered"

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

static void report(tile_report *r)
{
    char line[DIAG_VAL_MAX];
    tile_report_line(r, line, sizeof line);
    log_line("tile: %s", line);
    diag_set("tile", "%s", line);
    diag_set("tile appmeta", "%s %s", HB_TILE_APPMETA, r->appmeta_after ? "exists" : "missing");
    (void)diag_save();
}

int tile_install(const char *url, tile_report *rep)
{
    tile_report local;
    tile_report *r = rep ? rep : &local;
    fn_title_dir title_dir;
    fn_install_all install_all;
    hb_creds saved;

    memset(r, 0, sizeof *r);
    if (!url || !url[0]) url = HB_TILE_URL;
    diag_set("tile url", "%s", url);

    errno = 0;
    r->files = tile_files_put(HB_TILE_ROOT, url, hb_icon_png, sizeof hb_icon_png,
                              hb_start_html, HB_START_HTML_LEN);
    r->files_errno = errno;
    r->marker = access(TILE_MARK, F_OK) == 0;
    r->appmeta_before = r->appmeta_after = is_dir(HB_TILE_APPMETA);
    if (r->files < 0) {
        log_line("tile: cannot write %s/%s (errno %d)", HB_TILE_ROOT, HB_TILE_ID, r->files_errno);
        r->result = -1;
        r->code = r->files_errno;
        r->failed = "writing " HB_TILE_ROOT "/" HB_TILE_ID;
        report(r);
        return -1;
    }
    if (!tile_need_register(r->files, r->marker, r->appmeta_before)) {
        r->skipped = 1;
        report(r);
        return 0;
    }
    if (r->marker && !r->appmeta_before)
        log_line("tile: marker present but %s missing: registering again", HB_TILE_APPMETA);

    title_dir = (fn_title_dir)appinst_nid(NID_AppInstallTitleDir);
    install_all = (fn_install_all)appinst_nid(NID_AppInstallAll);
    r->titledir_found = title_dir != NULL;
    r->all_found = install_all != NULL;

    r->authid_before = kernel_get_ucred_authid(-1);
    r->authid_used = creds_elevate(HB_AUTHID_SHELLCORE, &saved, "tile");

    errno = 0;
    r->init_rc = sceAppInstUtilInitialize();
    r->init_errno = errno;
    if (r->init_rc) {
        r->failed = "installer init";
        r->code = r->init_rc;
    } else {
        int ok = 0;
        if (title_dir) {
            errno = 0;
            r->titledir_called = 1;
            r->titledir_rc = title_dir(HB_TILE_ID, HB_TILE_ROOT "/", NULL);
            r->titledir_errno = errno;
            ok = r->titledir_rc == 0;
        }
        if (!ok && install_all) {
            errno = 0;
            r->all_called = 1;
            r->all_rc = install_all(NULL);
            r->all_errno = errno;
            ok = r->all_rc == 0;
        }
        sceAppInstUtilTerminate();
        if (!ok) {
            if (r->all_called) { r->failed = "InstallAll"; r->code = r->all_rc; }
            else if (r->titledir_called) { r->failed = "TitleDir"; r->code = r->titledir_rc; }
            else { r->failed = "no install function"; r->code = -1; }
        }
    }
    creds_restore(&saved, "tile");

    r->appmeta_after = is_dir(HB_TILE_APPMETA);
    /* A non-zero code while the console holds metadata for the id (e.g.
     * "already installed") still leaves a working tile. */
    if (r->failed && r->appmeta_after && r->init_rc == 0) {
        log_line("tile: %s returned %#x but %s exists: treating as registered",
                 r->failed, (unsigned)r->code, HB_TILE_APPMETA);
        r->failed = NULL;
    }
    r->result = r->failed ? -1 : 0;
    log_line("tile: register %s -> %s (opens %s)", HB_TILE_ID,
             r->result ? "FAILED" : "ok", url);
    if (!r->result) {
        FILE *f = fopen(TILE_MARK, "w");
        if (f) fclose(f);
    } else {
        unlink(TILE_MARK);
    }
    report(r);
    return r->result;
}

int tile_uninstall(void)
{
    hb_creds saved;
    int rc;

    creds_elevate(HB_AUTHID_SHELLCORE, &saved, "tile");
    rc = sceAppInstUtilInitialize();
    if (!rc) {
        rc = sceAppInstUtilAppUnInstall(HB_TILE_ID);
        sceAppInstUtilTerminate();
    }
    creds_restore(&saved, "tile");
    log_line("tile: unregister %s -> %#x", HB_TILE_ID, (unsigned)rc);
    diag_set("tile", "removed on request (unregister %#x)", (unsigned)rc);
    unlink(TILE_MARK);
    tile_files_drop(HB_TILE_ROOT);
    return rc ? -1 : 0;
}
