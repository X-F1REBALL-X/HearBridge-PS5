/* Developed by X-F1REBALL-X.
 * Registers the tile folder with the console's app installer so it shows on
 * the home screen; selecting it follows deeplinkUri into the browser. */
#include "tile.h"
#include "icon_png.h"
#include "start_html.h"
#include "log.h"

#include <stdio.h>
#include <unistd.h>

int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallTitleDir(const char *title_id, const char *dir, void *opt);
int sceAppInstUtilAppUnInstall(const char *title_id);

#define TILE_MARK HB_TILE_ROOT "/" HB_TILE_ID "/sce_sys/.hb_registered"

int tile_install(const char *url)
{
    int changed, rc;

    if (!url || !url[0]) url = HB_TILE_URL;
    changed = tile_files_put(HB_TILE_ROOT, url, hb_icon_png, sizeof hb_icon_png,
                             hb_start_html, HB_START_HTML_LEN);
    if (changed < 0) {
        log_line("tile: cannot write %s/%s", HB_TILE_ROOT, HB_TILE_ID);
        return -1;
    }
    if (!changed && access(TILE_MARK, F_OK) == 0) {
        log_line("tile: home-screen tile already present");
        return 0;
    }
    rc = sceAppInstUtilInitialize();
    if (rc) {
        log_line("tile: installer init failed (%#x)", (unsigned)rc);
        return -1;
    }
    rc = sceAppInstUtilAppInstallTitleDir(HB_TILE_ID, HB_TILE_ROOT "/", NULL);
    sceAppInstUtilTerminate();
    log_line("tile: register %s -> %#x (opens %s)", HB_TILE_ID, (unsigned)rc, url);
    if (rc) return -1;
    {
        FILE *f = fopen(TILE_MARK, "w");
        if (f) fclose(f);
    }
    return 0;
}

int tile_uninstall(void)
{
    int rc = sceAppInstUtilInitialize();

    if (!rc) {
        rc = sceAppInstUtilAppUnInstall(HB_TILE_ID);
        sceAppInstUtilTerminate();
    }
    log_line("tile: unregister %s -> %#x", HB_TILE_ID, (unsigned)rc);
    unlink(TILE_MARK);
    tile_files_drop(HB_TILE_ROOT);
    return rc ? -1 : 0;
}
