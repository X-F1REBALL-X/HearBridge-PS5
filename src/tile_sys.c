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

/* libSceAppInstUtil is not a link-time dependency: the payload runtime
 * binds every DT_NEEDED module before main() and dies silently when one
 * cannot be loaded, so a missing or locked-down installer module would take
 * the whole payload (notification, page, audio) with it. It is loaded here
 * instead; without it there is just no tile. */
#define NID_Initialize         "540lotO7oHE"  /* sceAppInstUtilInitialize */
#define NID_Terminate          "kLLazhNh6d4"  /* sceAppInstUtilTerminate */
#define NID_AppUnInstall       "Sx4TTyrQccE"  /* sceAppInstUtilAppUnInstall */
#define NID_LoadStartModule    "wzvqT4UqKX8"  /* sceKernelLoadStartModule */
#define NID_AppInstallTitleDir "Wudg3Xe3heE"  /* sceAppInstUtilAppInstallTitleDir */
#define NID_AppInstallAll      "+scQA5stvjs"  /* sceAppInstUtilAppInstallAll */
#define NID_AppExists          "kUT4RpxclMQ"  /* sceAppInstUtilAppExists */

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

typedef int (*fn_load_start)(const char *, size_t, const void *, uint32_t, void *, int *);

/* Handle of libSceAppInstUtil.sprx, loading it on first use. */
static uint32_t appinst_handle(void)
{
    static const char *const kernels[] = { "libkernel_web.sprx", "libkernel.sprx", "libkernel_sys.sprx", NULL };
    static const char *const paths[] = { "/system/common/lib/libSceAppInstUtil.sprx",
                                         "libSceAppInstUtil.sprx", NULL };
    static int tried;
    uint32_t h = 0, kh = 0;
    fn_load_start load = NULL;
    int i, rv = 0, res = 0;

    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &h) == 0 && h) return h;
    if (tried) return 0;
    tried = 1;
    for (i = 0; kernels[i] && !load; i++)
        if (kernel_dynlib_handle(-1, kernels[i], &kh) == 0 && kh)
            load = (fn_load_start)kernel_dynlib_resolve(-1, kh, NID_LoadStartModule);
    if (!load) {
        log_line("tile: no sceKernelLoadStartModule, cannot load the installer module");
        diag_set("libSceAppInstUtil.sprx", "FAILED: sceKernelLoadStartModule not resolved");
        return 0;
    }
    for (i = 0; paths[i]; i++) {
        res = 0;
        rv = load(paths[i], 0, NULL, 0, NULL, &res);
        if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &h) == 0 && h) {
            log_line("tile: loaded %s (rv %d, handle %#x)", paths[i], rv, h);
            diag_set("libSceAppInstUtil.sprx", "loaded from %s", paths[i]);
            return h;
        }
    }
    log_line("tile: cannot load libSceAppInstUtil.sprx (rv %#x, res %#x) — no home screen icon",
             (unsigned)rv, (unsigned)res);
    diag_set("libSceAppInstUtil.sprx", "FAILED to load (rv %#x res %#x)", (unsigned)rv, (unsigned)res);
    return 0;
}

static void *appinst_nid(const char *nid)
{
    uint32_t h = appinst_handle();
    return h ? (void *)kernel_dynlib_resolve(-1, h, nid) : NULL;
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

static int unregister_title(void);

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
    {
        /* An icon installed by an older tile version (e.g. under Media):
         * uninstall it so the new param.json (Games) is taken. */
        char v[16] = "";
        int have = 0;
        FILE *f = fopen(HB_TILE_VER_PATH, "r");
        if (f) { have = fgets(v, (int)sizeof v, f) != NULL; fclose(f); }
        if (tile_needs_reinstall(have ? v : NULL, meta_exists(HB_TILE_ID))) {
            int rc = unregister_title();
            log_line("tile: icon from tile version %s, this is %s: reinstalling (unregister %#x)",
                     have ? v : "1", HB_TILE_VERSION, (unsigned)rc);
            if (!rc) usleep(2000 * 1000);
        }
    }
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
    ops.init = (int (*)(void))appinst_nid(NID_Initialize);
    ops.term = (int (*)(void))appinst_nid(NID_Terminate);
    if (!ops.init || !ops.term) {
        r->result = -1;
        r->failed = "loading libSceAppInstUtil.sprx";
        log_line("tile: installer module not available — skipping the icon, the page still works");
        report(r);
        return -1;
    }
    ops.title_dir = (int (*)(const char *, const char *, void *))appinst_nid(NID_AppInstallTitleDir);
    ops.install_all = (int (*)(void *))appinst_nid(NID_AppInstallAll);
    ops.app_exists = (int (*)(const char *, int *))appinst_nid(NID_AppExists);
    ops.meta_exists = meta_exists;

    r->authid_before = kernel_get_ucred_authid(-1);
    (void)tile_register_fallback(&ops, r, raise_creds, lower_creds, &saved);

    if (r->result == 0) {
        FILE *f = fopen(HB_TILE_VER_PATH, "w");
        if (f) { fprintf(f, "%s\n", HB_TILE_VERSION); fclose(f); }
    }
    log_line("tile: register %s -> %s (opens %s)", HB_TILE_ID,
             r->result ? "FAILED" : r->already ? "ok (was already installed)" : "ok", url);
    report(r);
    return r->result;
}

/* sceAppInstUtilAppUnInstall, with our own rights, then as ShellCore. */
static int unregister_title(void)
{
    hb_creds saved;
    int rc = -1;
    int (*init)(void) = (int (*)(void))appinst_nid(NID_Initialize);
    int (*term)(void) = (int (*)(void))appinst_nid(NID_Terminate);
    int (*uninstall)(const char *) = (int (*)(const char *))appinst_nid(NID_AppUnInstall);

    if (init && term && uninstall) {
        rc = init();
        if (!rc) {
            rc = uninstall(HB_TILE_ID);
            term();
        }
        if (rc) {
            /* Own rights refused (newer firmware): once more as ShellCore. */
            creds_elevate(HB_AUTHID_SHELLCORE, &saved, "tile");
            rc = init();
            if (!rc) {
                rc = uninstall(HB_TILE_ID);
                term();
            }
            creds_restore(&saved, "tile");
        }
    }
    return rc;
}

int tile_uninstall(void)
{
    int rc = unregister_title();
    log_line("tile: unregister %s -> %#x", HB_TILE_ID, (unsigned)rc);
    diag_set("tile", "removed on request (unregister %#x)", (unsigned)rc);
    unlink(HB_TILE_LEGACY_MARK);
    unlink(HB_TILE_VER_PATH);
    tile_files_drop(HB_TILE_ROOT);
    return rc ? -1 : 0;
}
