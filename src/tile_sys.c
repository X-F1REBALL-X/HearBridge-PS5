/* Developed by X-F1REBALL-X.
 * Registers the tile folder with the console's app installer so it shows on
 * the home screen; selecting it follows deeplinkUri into the browser.
 *
 * On EVERY start the files are rewritten and the folder is registered again
 * (no "already installed" shortcut, so an icon deleted from the home screen
 * always comes back). The per-title call is the same as in 1.0.1;
 * sceAppInstUtilAppInstallAll(0) (looked up at run time) is the fallback
 * when it fails. An error code from re-registering an installed title
 * counts as success when sceAppInstUtilAppExists (looked up at run time) or
 * /user/appmeta/<id> says the title is installed. */
#include "tile.h"
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
int sceAppInstUtilAppInstallTitleDir(const char *title_id, const char *dir, void *opt);
int sceAppInstUtilAppUnInstall(const char *title_id);

#define NID_AppInstallAll "+scQA5stvjs"  /* sceAppInstUtilAppInstallAll */
#define NID_AppExists     "kUT4RpxclMQ"  /* sceAppInstUtilAppExists */

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
    struct stat st;
    snprintf(p, sizeof p, "/user/appmeta/%s", title_id);
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

int tile_install(const char *url, tile_report *rep)
{
    tile_report local;
    tile_report *r = rep ? rep : &local;
    tile_ops ops;
    char line[600];

    memset(r, 0, sizeof *r);
    if (!url || !url[0]) url = HB_TILE_URL;

    r->legacy_marker = unlink(HB_TILE_LEGACY_MARK) == 0;
    errno = 0;
    if (tile_files_write(HB_TILE_ROOT, url, hb_icon_png, sizeof hb_icon_png,
                         hb_start_html, HB_START_HTML_LEN) != 0) {
        r->files_failed = 1;
        r->files_errno = errno;
        r->result = -1;
        r->code = r->files_errno;
        r->failed = "writing " HB_TILE_ROOT "/" HB_TILE_ID;
    } else {
        memset(&ops, 0, sizeof ops);
        ops.init = sceAppInstUtilInitialize;
        ops.term = sceAppInstUtilTerminate;
        ops.title_dir = sceAppInstUtilAppInstallTitleDir;
        ops.install_all = (int (*)(void *))appinst_nid(NID_AppInstallAll);
        ops.app_exists = (int (*)(const char *, int *))appinst_nid(NID_AppExists);
        ops.meta_exists = meta_exists;
        (void)tile_register(&ops, r);
    }
    tile_report_line(r, line, sizeof line);
    log_line("tile: %s", line);
    log_line("tile: register %s -> %s (opens %s)", HB_TILE_ID,
             r->result ? "FAILED" : r->already ? "ok (was already installed)" : "ok", url);
    return r->result;
}

int tile_uninstall(void)
{
    int rc = sceAppInstUtilInitialize();

    if (!rc) {
        rc = sceAppInstUtilAppUnInstall(HB_TILE_ID);
        sceAppInstUtilTerminate();
    }
    log_line("tile: unregister %s -> %#x", HB_TILE_ID, (unsigned)rc);
    unlink(HB_TILE_LEGACY_MARK);
    tile_files_drop(HB_TILE_ROOT);
    return rc ? -1 : 0;
}
